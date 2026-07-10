#include "helpers/x11_fake_client.hpp"
#include "helpers/x11_request_builder.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
namespace x11 = gw::protocol::x11;

struct Options {
  std::string socket_dir = "/tmp/.X11-unix";
  std::string display = "0";
  x11::ByteOrder order = x11::ByteOrder::LittleEndian;
  enum class Mode { Basic, Errors, Cleanup, CrossEndian } mode = Mode::Basic;
};

bool parse(int argc, char** argv, Options& options) {
  bool mode_seen = false;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--socket-dir" && index + 1 < argc) {
      options.socket_dir = argv[++index];
    } else if (argument == "--display" && index + 1 < argc) {
      options.display = argv[++index];
      if (!options.display.empty() && options.display.front() == ':') {
        options.display.erase(0, 1);
      }
    } else if (argument == "--byte-order" && index + 1 < argc) {
      const std::string_view value(argv[++index]);
      if (value == "little") options.order = x11::ByteOrder::LittleEndian;
      else if (value == "big") options.order = x11::ByteOrder::BigEndian;
      else return false;
    } else if (!mode_seen && argument == "--basic") {
      options.mode = Options::Mode::Basic;
      mode_seen = true;
    } else if (!mode_seen && argument == "--errors") {
      options.mode = Options::Mode::Errors;
      mode_seen = true;
    } else if (!mode_seen && argument == "--cleanup") {
      options.mode = Options::Mode::Cleanup;
      mode_seen = true;
    } else if (!mode_seen && argument == "--cross-endian") {
      options.mode = Options::Mode::CrossEndian;
      mode_seen = true;
    } else {
      return false;
    }
  }
  return !options.display.empty() &&
         options.display.find_first_not_of("0123456789") == std::string::npos;
}

struct Session {
  gw::test::X11FakeClient client;
  gw::test::X11RequestBuilder wire;
  x11::ByteOrder order;
  std::uint32_t base{0};

  Session(const std::string& socket, x11::ByteOrder byte_order)
      : client(socket), wire(byte_order), order(byte_order) {
    client.send_all(gw::test::make_setup_request(order));
    const auto reply = client.receive_setup_reply(order);
    if (reply.empty() || reply[0] != 1) throw std::runtime_error("setup failed");
    base = gw::test::read_wire_u32(reply.data() + 12, order);
  }

  void sync() {
    client.send_all(wire.get_input_focus());
    if (client.receive_server_packet(order)[0] != 1) {
      throw std::runtime_error("synchronization failed");
    }
  }

  std::uint32_t intern(std::string_view name) {
    client.send_all(wire.intern_atom(name));
    const auto reply = client.receive_server_packet(order);
    if (reply[0] != 1) throw std::runtime_error("InternAtom failed");
    return gw::test::read_wire_u32(reply.data() + 8, order);
  }
};

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void basic(const std::string& socket, x11::ByteOrder order) {
  Session session(socket, order);
  const auto window = session.base + 1;
  session.client.send_all(
      session.wire.create_window(window, 1, 2, 3, 80, 60));
  session.sync();
  session.client.send_all(session.wire.get_geometry(window));
  require(session.client.receive_server_packet(order)[0] == 1,
          "GetGeometry failed");
  const auto atom = session.intern("GW_M2_RAW");
  const std::vector<std::uint8_t> value{'g', 'l', 'a', 's', 's'};
  session.client.send_all(
      session.wire.change_property(0, window, atom, 31, 8, value, value.size()));
  session.sync();
  session.client.send_all(session.wire.get_property(window, atom, 31, 0, 16));
  const auto property = session.client.receive_server_packet(order);
  require(property[1] == 8 &&
              std::vector<std::uint8_t>(property.begin() + 32,
                                        property.begin() + 37) == value,
          "property round trip failed");
  session.client.send_all(session.wire.destroy_window(window));
  session.sync();
}

void errors(const std::string& socket) {
  Session session(socket, x11::ByteOrder::LittleEndian);
  session.client.send_all(session.wire.raw(99, 0));
  const auto bad_request = session.client.receive_server_packet(session.order);
  require(bad_request[0] == 0 && bad_request[1] == 1,
          "unsupported opcode did not return BadRequest");
  session.client.send_all(session.wire.get_geometry(0xdeadbeef));
  const auto bad_drawable = session.client.receive_server_packet(session.order);
  require(bad_drawable[0] == 0 && bad_drawable[1] == 9,
          "invalid drawable did not return BadDrawable");
  session.sync();
}

void cleanup(const std::string& socket) {
  std::uint32_t expected_base = 0;
  {
    Session owner(socket, x11::ByteOrder::LittleEndian);
    expected_base = owner.base;
    owner.client.send_all(
        owner.wire.create_window(owner.base + 1, 1, 0, 0, 10, 10));
    owner.sync();
  }

  for (int attempt = 0; attempt < 100; ++attempt) {
    Session replacement(socket, x11::ByteOrder::LittleEndian);
    if (replacement.base == expected_base) {
      replacement.client.send_all(replacement.wire.create_window(
          replacement.base + 1, 1, 0, 0, 10, 10));
      replacement.sync();
      replacement.client.send_all(
          replacement.wire.get_geometry(replacement.base + 1));
      require(replacement.client.receive_server_packet(replacement.order)[0] == 1,
              "reused XID encountered stale resource state");
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  throw std::runtime_error("resource base was not reused after cleanup");
}

void cross_endian(const std::string& socket) {
  Session little(socket, x11::ByteOrder::LittleEndian);
  Session big(socket, x11::ByteOrder::BigEndian);
  const auto window = little.base + 2;
  little.client.send_all(
      little.wire.create_window(window, 1, 0, 0, 10, 10));
  little.sync();
  const auto atom = little.intern("GW_M2_ENDIAN");
  const std::vector<std::uint8_t> encoded{0x04, 0x03, 0x02, 0x01};
  little.client.send_all(
      little.wire.change_property(0, window, atom, 6, 32, encoded, 1));
  little.sync();
  big.client.send_all(big.wire.get_property(window, atom, 6, 0, 1));
  const auto reply = big.client.receive_server_packet(big.order);
  require(reply.size() == 36 && reply[32] == 0x01 && reply[33] == 0x02 &&
              reply[34] == 0x03 && reply[35] == 0x04,
          "cross-endian format-32 value mismatch");
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parse(argc, argv, options)) {
    std::cerr << "Usage: x11_milestone2_probe [--socket-dir PATH] "
                 "[--display :N] [--byte-order little|big] "
                 "[--basic|--errors|--cleanup|--cross-endian]\n";
    return 2;
  }
  try {
    const std::string socket = options.socket_dir + "/X" + options.display;
    switch (options.mode) {
      case Options::Mode::Basic: basic(socket, options.order); break;
      case Options::Mode::Errors: errors(socket); break;
      case Options::Mode::Cleanup: cleanup(socket); break;
      case Options::Mode::CrossEndian: cross_endian(socket); break;
    }
    std::cout << "milestone2 probe passed\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "x11_milestone2_probe: " << exception.what() << '\n';
    return 1;
  }
}
