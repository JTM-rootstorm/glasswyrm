#include "helpers/x11_fake_client.hpp"
#include "helpers/x11_request_builder.hpp"
#include "protocol/x11/core.hpp"
#include "protocol/x11/lifecycle_request.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace x11 = gw::protocol::x11;

struct Options {
  std::string socket_dir = "/tmp/.X11-unix";
  std::string display = "0";
  x11::ByteOrder order = x11::ByteOrder::LittleEndian;
};

bool parse(const int argc, char** argv, Options& options) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--socket-dir" && index + 1 < argc) {
      options.socket_dir = argv[++index];
    } else if (argument == "--display" && index + 1 < argc) {
      options.display = argv[++index];
      if (!options.display.empty() && options.display.front() == ':')
        options.display.erase(0, 1);
    } else if (argument == "--byte-order" && index + 1 < argc) {
      const std::string_view value(argv[++index]);
      if (value == "little") options.order = x11::ByteOrder::LittleEndian;
      else if (value == "big") options.order = x11::ByteOrder::BigEndian;
      else return false;
    } else {
      return false;
    }
  }
  return !options.display.empty() &&
         options.display.find_first_not_of("0123456789") == std::string::npos;
}

void require(const bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void put16(std::vector<std::uint8_t>& bytes, const std::uint16_t value,
           const x11::ByteOrder order) {
  if (order == x11::ByteOrder::LittleEndian) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  } else {
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value));
  }
}

void put32(std::vector<std::uint8_t>& bytes, const std::uint32_t value,
           const x11::ByteOrder order) {
  if (order == x11::ByteOrder::LittleEndian) {
    for (unsigned shift = 0; shift < 32; shift += 8)
      bytes.push_back(static_cast<std::uint8_t>(value >> shift));
  } else {
    for (int shift = 24; shift >= 0; shift -= 8)
      bytes.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

std::vector<std::uint8_t> window_request(
    const gw::test::X11RequestBuilder& wire, const x11::CoreOpcode opcode,
    const std::uint32_t window, const x11::ByteOrder order) {
  std::vector<std::uint8_t> body;
  put32(body, window, order);
  return wire.raw(static_cast<std::uint8_t>(opcode), 0, body);
}

std::vector<std::uint8_t> configure(
    const gw::test::X11RequestBuilder& wire, const std::uint32_t window,
    const std::uint32_t sibling, const x11::ByteOrder order) {
  constexpr std::uint16_t mask =
      x11::ConfigureX | x11::ConfigureY | x11::ConfigureWidth |
      x11::ConfigureHeight | x11::ConfigureSibling | x11::ConfigureStackMode;
  std::vector<std::uint8_t> body;
  put32(body, window, order);
  put16(body, mask, order);
  put16(body, 0, order);
  put32(body, 70, order);
  put32(body, 80, order);
  put32(body, 360, order);
  put32(body, 240, order);
  put32(body, sibling, order);
  put32(body, static_cast<std::uint32_t>(x11::CoreStackMode::Above), order);
  return wire.raw(static_cast<std::uint8_t>(x11::CoreOpcode::ConfigureWindow),
                  0, body);
}

struct Session {
  gw::test::X11FakeClient client;
  gw::test::X11RequestBuilder wire;
  x11::ByteOrder order;
  std::uint32_t base{};

  Session(const std::string& socket, const x11::ByteOrder byte_order)
      : client(socket), wire(byte_order), order(byte_order) {
    client.send_all(gw::test::make_setup_request(order));
    const auto setup = client.receive_setup_reply(order);
    require(setup[0] == 1, "X11 setup failed");
    base = gw::test::read_wire_u32(setup.data() + 12, order);
  }

  std::vector<std::uint8_t> packet() {
    return client.receive_server_packet(order);
  }

  void sync(const std::uint16_t sequence) {
    client.send_all(wire.get_input_focus());
    const auto reply = packet();
    require(reply[0] == 1 &&
                gw::test::read_wire_u16(reply.data() + 2, order) == sequence,
            "synchronization sequence mismatch");
  }
};

void run(const std::string& socket, const x11::ByteOrder order) {
  Session session(socket, order);
  const auto first = session.base + 1;
  const auto second = session.base + 2;
  session.client.send_all(session.wire.create_window(first, 1, 10, 20, 320, 200));
  session.client.send_all(session.wire.create_window(second, 1, 30, 40, 200, 120));
  session.sync(3);

  constexpr std::array<std::uint32_t, 1> structure_notify{1U << 17U};
  session.client.send_all(session.wire.change_window_attributes(
      first, 1U << 11U, structure_notify));
  session.sync(5);

  session.client.send_all(
      window_request(session.wire, x11::CoreOpcode::MapWindow, first, order));
  auto event = session.packet();
  require(event[0] == 19 &&
              gw::test::read_wire_u16(event.data() + 2, order) == 6,
          "MapNotify mismatch");

  session.client.send_all(
      window_request(session.wire, x11::CoreOpcode::MapWindow, second, order));
  session.sync(8);

  session.client.send_all(configure(session.wire, first, second, order));
  event = session.packet();
  require(event[0] == 22 &&
              gw::test::read_wire_u16(event.data() + 2, order) == 9,
          "ConfigureNotify mismatch");
  session.client.send_all(session.wire.get_geometry(first));
  auto reply = session.packet();
  require(reply[0] == 1 &&
              gw::test::read_wire_u16(reply.data() + 16, order) == 360 &&
              gw::test::read_wire_u16(reply.data() + 18, order) == 240,
          "committed geometry mismatch");

  session.client.send_all(window_request(
      session.wire, x11::CoreOpcode::UnmapWindow, first, order));
  event = session.packet();
  require(event[0] == 18 &&
              gw::test::read_wire_u16(event.data() + 2, order) == 11,
          "UnmapNotify mismatch");

  session.client.send_all(session.wire.destroy_window(first));
  event = session.packet();
  require(event[0] == 17 &&
              gw::test::read_wire_u16(event.data() + 2, order) == 12,
          "DestroyNotify mismatch");
  session.sync(13);
  session.client.send_all(session.wire.query_tree(1));
  reply = session.packet();
  require(reply[0] == 1 &&
              gw::test::read_wire_u16(reply.data() + 16, order) == 1 &&
              gw::test::read_wire_u32(reply.data() + 32, order) == second,
          "committed root tree mismatch");
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parse(argc, argv, options)) {
    std::cerr << "Usage: x11_milestone6_probe [--socket-dir PATH] "
                 "[--display :N] [--byte-order little|big]\n";
    return 2;
  }
  try {
    run(options.socket_dir + "/X" + options.display, options.order);
    std::cout << "milestone6 probe passed byte_order="
              << (options.order == x11::ByteOrder::LittleEndian ? "little"
                                                                 : "big")
              << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "x11_milestone6_probe: " << error.what() << '\n';
    return 1;
  }
}
