#include "helpers/test_support.hpp"
#include "helpers/x11_fake_client.hpp"
#include "helpers/x11_request_builder.hpp"
#include "integration/server_fixture.hpp"
#include "protocol/x11/byte_cursor.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace x11 = gw::protocol::x11;
using gw::test::require;

struct Session {
  gw::test::X11FakeClient client;
  gw::test::X11RequestBuilder requests;
  x11::ByteOrder order;
  std::uint32_t resource_base{};

  Session(const std::string& socket, const x11::ByteOrder byte_order)
      : client(socket), requests(byte_order), order(byte_order) {
    client.send_all(gw::test::make_setup_request(order));
    const auto setup = client.receive_setup_reply(order);
    require(setup[0] == 1, "M11 isolation client setup succeeds");
    resource_base = gw::test::read_wire_u32(setup.data() + 12, order);
  }

  void sync() {
    client.send_all(requests.get_input_focus());
    require(client.receive_server_packet(order)[0] == 1,
            "M11 isolation client remains responsive");
  }
};

std::size_t descriptor_count(const pid_t pid) {
  const auto path =
      std::filesystem::path("/proc") / std::to_string(pid) / "fd";
  return static_cast<std::size_t>(
      std::distance(std::filesystem::directory_iterator(path),
                    std::filesystem::directory_iterator{}));
}

std::vector<std::uint8_t> set_owner(const Session& session,
                                    const std::uint32_t owner,
                                    const std::uint32_t selection) {
  x11::ByteWriter body(session.order);
  body.write_u32(owner);
  body.write_u32(selection);
  body.write_u32(0);
  return session.requests.raw(22, 0, std::move(body).take());
}

std::vector<std::uint8_t> get_owner(const Session& session,
                                    const std::uint32_t selection) {
  x11::ByteWriter body(session.order);
  body.write_u32(selection);
  return session.requests.raw(23, 0, std::move(body).take());
}

void require_error(Session& session, const std::vector<std::uint8_t>& request,
                   const std::uint8_t code, const char* message) {
  session.client.send_all(request);
  const auto packet = session.client.receive_server_packet(session.order);
  require(packet.size() == 32 && packet[0] == 0 && packet[1] == code, message);
}

void malformed_selection_isolation(const std::string& socket,
                                   Session& healthy) {
  Session malformed(socket, x11::ByteOrder::BigEndian);
  constexpr std::uint32_t primary = 1;
  require_error(malformed,
                set_owner(malformed, malformed.resource_base + 500, primary),
                3, "invalid selection owner returns BadWindow");
  require_error(malformed,
                set_owner(malformed, 0, UINT32_C(0xfefefefe)), 5,
                "invalid selection atom returns BadAtom");

  x11::ByteWriter short_conversion(malformed.order);
  short_conversion.write_u32(malformed.resource_base + 1);
  short_conversion.write_u32(primary);
  short_conversion.write_u32(31);
  short_conversion.write_u32(0);
  require_error(malformed,
                malformed.requests.raw(24, 0,
                                       std::move(short_conversion).take()),
                16, "truncated ConvertSelection returns BadLength");
  malformed.sync();
  healthy.sync();
  healthy.client.send_all(get_owner(healthy, primary));
  const auto owner = healthy.client.receive_server_packet(healthy.order);
  require(owner[0] == 1 &&
              gw::test::read_wire_u32(owner.data() + 8, healthy.order) == 0,
          "malformed selection requests neither mutate ownership nor harm peers");
}

void slow_client_isolation(const std::string& socket, Session& healthy) {
  Session slow(socket, x11::ByteOrder::LittleEndian);
  const std::string large_name(65000, 'S');
  slow.client.send_all(slow.requests.intern_atom(large_name));
  const auto atom_reply = slow.client.receive_server_packet(slow.order);
  const auto atom = gw::test::read_wire_u32(atom_reply.data() + 8, slow.order);
  const auto request = slow.requests.get_atom_name(atom);
  std::vector<std::uint8_t> pipeline;
  for (int index = 0; index < 17; ++index)
    pipeline.insert(pipeline.end(), request.begin(), request.end());
  slow.client.send_all(pipeline);
  require(slow.client.peer_closed(),
          "non-reading X11 client is isolated at the output cap");
  healthy.sync();
}

void resource_repetition(const std::string& socket,
                         const pid_t server_pid, Session& healthy) {
  constexpr std::uint32_t primary = 1;
  constexpr std::uint32_t string_atom = 31;
  const auto baseline = descriptor_count(server_pid);
  for (int iteration = 0; iteration < 64; ++iteration) {
    Session transient(socket, (iteration & 1) == 0
                                  ? x11::ByteOrder::LittleEndian
                                  : x11::ByteOrder::BigEndian);
    const auto window = transient.resource_base + 1;
    transient.client.send_all(
        transient.requests.create_window(window, 1, 0, 0, 32, 24));
    transient.sync();
    const std::array<std::uint8_t, 4> property{'M', '1', '1', '!'};
    transient.client.send_all(transient.requests.change_property(
        0, window, primary, string_atom, 8, property, property.size()));
    transient.client.send_all(set_owner(transient, window, primary));
    transient.sync();
  }

  bool descriptors_released = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (descriptor_count(server_pid) == baseline) {
      descriptors_released = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  require(descriptors_released,
          "repeated resource-owning clients release every descriptor");

  healthy.client.send_all(get_owner(healthy, primary));
  const auto owner = healthy.client.receive_server_packet(healthy.order);
  healthy.client.send_all(healthy.requests.query_tree(1));
  const auto tree = healthy.client.receive_server_packet(healthy.order);
  require(gw::test::read_wire_u32(owner.data() + 8, healthy.order) == 0 &&
              gw::test::read_wire_u16(tree.data() + 16, healthy.order) == 0,
          "disconnect repetition releases selection, window, and property state");
}

}  // namespace

int main(int argc, char** argv) {
  require(argc == 2, "daemon path argument is required");
  gw::test::ServerProcess server(argv[1]);
  try {
    Session healthy(server.socket_path(), x11::ByteOrder::LittleEndian);
    malformed_selection_isolation(server.socket_path(), healthy);
    slow_client_isolation(server.socket_path(), healthy);
    resource_repetition(server.socket_path(), server.pid(), healthy);
    healthy.sync();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "M11 isolation test: " << error.what()
              << "\nserver log:\n" << server.log_contents();
    return 1;
  }
}
