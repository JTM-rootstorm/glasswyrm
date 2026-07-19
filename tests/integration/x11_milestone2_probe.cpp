#include "helpers/x11_fake_client.hpp"
#include "helpers/x11_request_builder.hpp"

#include <chrono>
#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
namespace x11 = gw::protocol::x11;

struct RootDimensions {
  std::uint16_t width{};
  std::uint16_t height{};
};

struct Options {
  std::string socket_dir = "/tmp/.X11-unix";
  std::string display = "0";
  x11::ByteOrder order = x11::ByteOrder::LittleEndian;
  enum class Mode { Basic, Errors, Cleanup, CrossEndian } mode = Mode::Basic;
  std::optional<RootDimensions> expected_root;
};

bool parse_dimension(std::string_view text, std::uint16_t& result) {
  unsigned value = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() || value == 0 ||
      value > std::numeric_limits<std::uint16_t>::max()) {
    return false;
  }
  result = static_cast<std::uint16_t>(value);
  return true;
}

bool parse_root_dimensions(std::string_view text, RootDimensions& result) {
  const auto separator = text.find('x');
  return separator != std::string_view::npos && separator != 0 &&
         separator + 1 < text.size() &&
         text.find('x', separator + 1) == std::string_view::npos &&
         parse_dimension(text.substr(0, separator), result.width) &&
         parse_dimension(text.substr(separator + 1), result.height);
}

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
    } else if (argument == "--expect-root" && index + 1 < argc &&
               !options.expected_root.has_value()) {
      RootDimensions expected;
      if (!parse_root_dimensions(argv[++index], expected)) return false;
      options.expected_root = expected;
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
         options.display.find_first_not_of("0123456789") == std::string::npos &&
         (!options.expected_root.has_value() ||
          options.mode == Options::Mode::Basic);
}

RootDimensions setup_root_dimensions(const std::vector<std::uint8_t>& reply,
                                     x11::ByteOrder order) {
  if (reply.size() < 40 || reply[28] == 0) {
    throw std::runtime_error("setup reply has no complete screen metadata");
  }
  const auto vendor_size = gw::test::read_wire_u16(reply.data() + 24, order);
  const auto padded_vendor_size =
      (static_cast<std::size_t>(vendor_size) + 3U) & ~std::size_t{3U};
  const auto screen_offset =
      40U + padded_vendor_size + static_cast<std::size_t>(reply[29]) * 8U;
  if (screen_offset > reply.size() || reply.size() - screen_offset < 24) {
    throw std::runtime_error("setup reply screen metadata is truncated");
  }
  return {gw::test::read_wire_u16(reply.data() + screen_offset + 20, order),
          gw::test::read_wire_u16(reply.data() + screen_offset + 22, order)};
}

struct Session {
  gw::test::X11FakeClient client;
  gw::test::X11RequestBuilder wire;
  x11::ByteOrder order;
  std::uint32_t base{0};
  std::vector<std::uint8_t> setup_reply;

  Session(const std::string& socket, x11::ByteOrder byte_order)
      : client(socket), wire(byte_order), order(byte_order) {
    client.send_all(gw::test::make_setup_request(order));
    setup_reply = client.receive_setup_reply(order);
    if (setup_reply.empty() || setup_reply[0] != 1)
      throw std::runtime_error("setup failed");
    base = gw::test::read_wire_u32(setup_reply.data() + 12, order);
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

void basic(const std::string& socket, x11::ByteOrder order,
           const std::optional<RootDimensions>& expected_root) {
  Session session(socket, order);
  if (expected_root.has_value()) {
    const auto actual = setup_root_dimensions(session.setup_reply, order);
    if (actual.width != expected_root->width ||
        actual.height != expected_root->height) {
      throw std::runtime_error(
          "root dimensions mismatch: expected " +
          std::to_string(expected_root->width) + "x" +
          std::to_string(expected_root->height) + ", got " +
          std::to_string(actual.width) + "x" + std::to_string(actual.height));
    }
  }
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
                 "[--expect-root WIDTHxHEIGHT] "
                 "[--basic|--errors|--cleanup|--cross-endian]\n";
    return 2;
  }
  try {
    const std::string socket = options.socket_dir + "/X" + options.display;
    switch (options.mode) {
      case Options::Mode::Basic:
        basic(socket, options.order, options.expected_root);
        break;
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
