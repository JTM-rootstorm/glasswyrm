#include "helpers/x11_fake_client.hpp"
#include "helpers/x11_request_builder.hpp"
#include "protocol/x11/core.hpp"

#include <cstdint>
#include <array>
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
  std::string display;
  std::string scenario;
  x11::ByteOrder order = x11::ByteOrder::LittleEndian;
};

bool parse(int argc, char **argv, Options &options) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--socket-dir" && index + 1 < argc)
      options.socket_dir = argv[++index];
    else if (argument == "--display" && index + 1 < argc) {
      options.display = argv[++index];
      if (!options.display.empty() && options.display.front() == ':')
        options.display.erase(0, 1);
    } else if (argument == "--byte-order" && index + 1 < argc) {
      const std::string_view value(argv[++index]);
      if (value == "little") options.order = x11::ByteOrder::LittleEndian;
      else if (value == "big") options.order = x11::ByteOrder::BigEndian;
      else return false;
    } else if (argument == "--scenario" && index + 1 < argc)
      options.scenario = argv[++index];
    else
      return false;
  }
  return !options.display.empty() &&
         options.display.find_first_not_of("0123456789") == std::string::npos &&
         (options.scenario == "draw" || options.scenario == "errors" ||
          options.scenario == "exposure");
}

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

void put16(std::vector<std::uint8_t> &bytes, std::uint16_t value,
           x11::ByteOrder order) {
  if (order == x11::ByteOrder::LittleEndian) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  } else {
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value));
  }
}

void put32(std::vector<std::uint8_t> &bytes, std::uint32_t value,
           x11::ByteOrder order) {
  if (order == x11::ByteOrder::LittleEndian) {
    for (unsigned shift = 0; shift < 32; shift += 8)
      bytes.push_back(static_cast<std::uint8_t>(value >> shift));
  } else {
    for (int shift = 24; shift >= 0; shift -= 8)
      bytes.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

struct Session {
  gw::test::X11FakeClient client;
  gw::test::X11RequestBuilder wire;
  x11::ByteOrder order;
  std::uint32_t base{};
  std::uint16_t sequence{};

  Session(const std::string &socket, x11::ByteOrder byte_order)
      : client(socket), wire(byte_order), order(byte_order) {
    client.send_all(gw::test::make_setup_request(order));
    const auto setup = client.receive_setup_reply(order);
    require(setup[0] == 1, "X11 setup failed");
    base = gw::test::read_wire_u32(setup.data() + 12, order);
  }

  void send(const std::vector<std::uint8_t> &request) {
    client.send_all(request);
    ++sequence;
  }

  std::vector<std::uint8_t> packet() {
    return client.receive_server_packet(order);
  }

  void sync() {
    send(wire.get_input_focus());
    const auto reply = packet();
    require(reply[0] == 1 &&
                gw::test::read_wire_u16(reply.data() + 2, order) == sequence,
            "synchronization reply mismatch");
  }
};

std::vector<std::uint8_t> create_pixmap(
    const gw::test::X11RequestBuilder &wire, std::uint32_t pixmap,
    std::uint32_t drawable, x11::ByteOrder order) {
  std::vector<std::uint8_t> body;
  put32(body, pixmap, order);
  put32(body, drawable, order);
  put16(body, 16, order);
  put16(body, 16, order);
  return wire.raw(53, 24, body);
}

std::vector<std::uint8_t> create_gc(
    const gw::test::X11RequestBuilder &wire, std::uint32_t gc,
    std::uint32_t drawable, x11::ByteOrder order, bool exposures = true) {
  constexpr std::uint32_t foreground = 1U << 2U;
  constexpr std::uint32_t graphics_exposures = 1U << 16U;
  std::vector<std::uint8_t> body;
  put32(body, gc, order);
  put32(body, drawable, order);
  put32(body, foreground | graphics_exposures, order);
  put32(body, 0x00d94c4cU, order);
  put32(body, exposures ? 1U : 0U, order);
  return wire.raw(55, 0, body);
}

std::vector<std::uint8_t> fill(
    const gw::test::X11RequestBuilder &wire, std::uint32_t drawable,
    std::uint32_t gc, x11::ByteOrder order) {
  std::vector<std::uint8_t> body;
  put32(body, drawable, order);
  put32(body, gc, order);
  put16(body, 8, order);
  put16(body, 8, order);
  put16(body, 72, order);
  put16(body, 48, order);
  return wire.raw(70, 0, body);
}

std::vector<std::uint8_t> put_image(
    const gw::test::X11RequestBuilder &wire, std::uint32_t drawable,
    std::uint32_t gc, x11::ByteOrder order) {
  std::vector<std::uint8_t> body;
  put32(body, drawable, order);
  put32(body, gc, order);
  put16(body, 2, order);
  put16(body, 2, order);
  put16(body, 0, order);
  put16(body, 0, order);
  body.push_back(0);
  body.push_back(24);
  put16(body, 0, order);
  // Image data follows the server's advertised LSB-first image order even for
  // a big-endian request stream.
  constexpr std::uint32_t pixels[] = {0x00112233U, 0x00445566U,
                                      0x00778899U, 0x00aabbccU};
  for (const auto pixel : pixels)
    for (unsigned shift = 0; shift < 32; shift += 8)
      body.push_back(static_cast<std::uint8_t>(pixel >> shift));
  return wire.raw(72, 2, body);
}

std::vector<std::uint8_t> copy_area(
    const gw::test::X11RequestBuilder &wire, std::uint32_t source,
    std::uint32_t destination, std::uint32_t gc, x11::ByteOrder order,
    std::uint16_t width = 16) {
  std::vector<std::uint8_t> body;
  put32(body, source, order);
  put32(body, destination, order);
  put32(body, gc, order);
  put16(body, 0, order);
  put16(body, 0, order);
  put16(body, 24, order);
  put16(body, 32, order);
  put16(body, width, order);
  put16(body, 16, order);
  return wire.raw(62, 0, body);
}

std::vector<std::uint8_t> clear_area(
    const gw::test::X11RequestBuilder &wire, std::uint32_t window,
    x11::ByteOrder order) {
  std::vector<std::uint8_t> body;
  put32(body, window, order);
  put16(body, 4, order);
  put16(body, 4, order);
  put16(body, 24, order);
  put16(body, 24, order);
  return wire.raw(61, 1, body);
}

std::vector<std::uint8_t> resource_request(
    const gw::test::X11RequestBuilder &wire, std::uint8_t opcode,
    std::uint32_t resource, x11::ByteOrder order) {
  std::vector<std::uint8_t> body;
  put32(body, resource, order);
  return wire.raw(opcode, 0, body);
}

void prepare(Session &session, std::uint32_t window, std::uint32_t pixmap,
             std::uint32_t gc) {
  session.send(session.wire.create_window(window, 1, 24, 32, 160, 120));
  session.send(create_pixmap(session.wire, pixmap, 1, session.order));
  session.send(create_gc(session.wire, gc, pixmap, session.order));
  session.send(put_image(session.wire, pixmap, gc, session.order));
  session.send(fill(session.wire, window, gc, session.order));
}

void run_draw(Session &session) {
  const auto window = session.base + 1, pixmap = session.base + 2,
             gc = session.base + 3;
  prepare(session, window, pixmap, gc);
  session.send(copy_area(session.wire, pixmap, window, gc, session.order));
  session.send(resource_request(session.wire, 8, window, session.order));
  session.sync();
  session.send(resource_request(session.wire, 60, gc, session.order));
  session.send(resource_request(session.wire, 54, pixmap, session.order));
  session.sync();
}

void run_exposure(Session &session) {
  const auto window = session.base + 1, pixmap = session.base + 2,
             gc = session.base + 3;
  prepare(session, window, pixmap, gc);
  constexpr std::uint32_t exposure_mask = 1U << 15U;
  constexpr std::array<std::uint32_t, 1> exposure_values{exposure_mask};
  session.send(session.wire.change_window_attributes(window, 1U << 11U,
                                                       exposure_values));
  session.send(resource_request(session.wire, 8, window, session.order));
  auto event = session.packet();
  require((event[0] & 0x7fU) == 12, "Expose event missing");
  session.send(clear_area(session.wire, window, session.order));
  event = session.packet();
  require((event[0] & 0x7fU) == 12, "ClearArea Expose event missing");
  session.send(copy_area(session.wire, pixmap, window, gc, session.order, 0));
  event = session.packet();
  require((event[0] & 0x7fU) == 14, "NoExpose event missing");
  session.sync();
}

void run_errors(Session &session) {
  const auto gc = session.base + 1;
  session.send(create_gc(session.wire, gc, 1, session.order));
  session.send(fill(session.wire, 0x7ffffffeU, gc, session.order));
  const auto error = session.packet();
  require(error[0] == 0 && error[1] == 9 &&
              gw::test::read_wire_u16(error.data() + 2, session.order) ==
                  session.sequence,
          "BadDrawable error missing");
  session.sync();
}

}  // namespace

int main(int argc, char **argv) {
  Options options;
  if (!parse(argc, argv, options)) {
    std::cerr << "Usage: x11_milestone7_probe [--socket-dir PATH] --display "
                 ":N --byte-order little|big --scenario draw|errors|exposure\n";
    return 2;
  }
  try {
    Session session(options.socket_dir + "/X" + options.display, options.order);
    if (options.scenario == "draw") run_draw(session);
    else if (options.scenario == "exposure") run_exposure(session);
    else run_errors(session);
    std::cout << "{\"completed\":true,\"scenario\":\""
              << options.scenario << "\",\"byte_order\":\""
              << (options.order == x11::ByteOrder::LittleEndian ? "little"
                                                                 : "big")
              << "\",\"image_order\":\"lsb-first\"}\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "x11_milestone7_probe: " << error.what() << '\n';
    return 1;
  }
}
