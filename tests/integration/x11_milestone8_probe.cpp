#include "helpers/synthetic_input_client.hpp"
#include "helpers/x11_fake_client.hpp"
#include "helpers/x11_request_builder.hpp"
#include "protocol/x11/core.hpp"
#include "protocol/x11/event_mask.hpp"

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
  std::string display;
  std::string input_socket;
  std::string scenario;
  x11::ByteOrder order{x11::ByteOrder::LittleEndian};
};

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

bool parse(int argc, char** argv, Options& options) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--display" && index + 1 < argc) {
      options.display = argv[++index];
      if (!options.display.empty() && options.display.front() == ':')
        options.display.erase(0, 1);
    } else if (argument == "--socket-dir" && index + 1 < argc) {
      options.socket_dir = argv[++index];
    } else if (argument == "--input-socket" && index + 1 < argc) {
      options.input_socket = argv[++index];
    } else if (argument == "--byte-order" && index + 1 < argc) {
      const std::string_view value(argv[++index]);
      if (value == "little") options.order = x11::ByteOrder::LittleEndian;
      else if (value == "big") options.order = x11::ByteOrder::BigEndian;
      else return false;
    } else if (argument == "--scenario" && index + 1 < argc) {
      options.scenario = argv[++index];
    } else return false;
  }
  return !options.display.empty() && !options.input_socket.empty() &&
         (options.scenario == "routing" || options.scenario == "propagation" ||
          options.scenario == "state" || options.scenario == "errors");
}

struct Session {
  gw::test::X11FakeClient client;
  gw::test::X11RequestBuilder wire;
  x11::ByteOrder order;
  std::uint32_t base{};
  std::uint32_t root{};
  std::uint16_t sequence{};

  Session(const std::string& socket, x11::ByteOrder byte_order)
      : client(socket), wire(byte_order), order(byte_order) {
    client.send_all(gw::test::make_setup_request(order));
    const auto setup = client.receive_setup_reply(order);
    require(setup[0] == 1, "X11 setup failed");
    base = gw::test::read_wire_u32(setup.data() + 12, order);
    // The first screen starts after the fixed setup header, vendor and format
    // list. All repository setup profiles advertise one zero-length vendor and
    // one pixmap format, placing root at byte 72.
    root = gw::test::read_wire_u32(setup.data() + 72, order);
  }
  void send(const std::vector<std::uint8_t>& request) {
    client.send_all(request); ++sequence;
  }
  std::vector<std::uint8_t> packet() { return client.receive_server_packet(order); }
  void sync() {
    send(wire.get_input_focus());
    for (;;) {
      const auto reply = packet();
      if (reply[0] == 1) {
        require(gw::test::read_wire_u16(reply.data() + 2, order) == sequence,
                "synchronization sequence mismatch");
        return;
      }
    }
  }
};

std::vector<std::uint8_t> map_request(const gw::test::X11RequestBuilder& wire,
                                      std::uint32_t window,
                                      x11::ByteOrder order) {
  std::vector<std::uint8_t> body(4);
  if (order == x11::ByteOrder::LittleEndian) {
    body[0] = static_cast<std::uint8_t>(window);
    body[1] = static_cast<std::uint8_t>(window >> 8U);
    body[2] = static_cast<std::uint8_t>(window >> 16U);
    body[3] = static_cast<std::uint8_t>(window >> 24U);
  } else {
    body[0] = static_cast<std::uint8_t>(window >> 24U);
    body[1] = static_cast<std::uint8_t>(window >> 16U);
    body[2] = static_cast<std::uint8_t>(window >> 8U);
    body[3] = static_cast<std::uint8_t>(window);
  }
  return wire.raw(8, 0, body);
}

struct Event {
  std::uint8_t type{};
  std::uint8_t detail{};
  std::uint16_t sequence{};
  std::uint32_t time{};
  std::uint32_t event{};
  std::uint16_t state{};
};

Event parse_event(const std::vector<std::uint8_t>& bytes,
                  x11::ByteOrder order) {
  require(bytes.size() == 32 && bytes[0] >= 2 && bytes[0] <= 10,
          "unexpected input event packet");
  Event result{bytes[0], bytes[1],
               gw::test::read_wire_u16(bytes.data() + 2, order)};
  if (bytes[0] <= 8) {
    result.time = gw::test::read_wire_u32(bytes.data() + 4, order);
    result.event = gw::test::read_wire_u32(bytes.data() + 12, order);
    result.state = gw::test::read_wire_u16(bytes.data() + 28, order);
  } else {
    result.event = gw::test::read_wire_u32(bytes.data() + 4, order);
  }
  return result;
}

Event wait_for(Session& session, std::uint8_t type) {
  for (int attempt = 0; attempt != 16; ++attempt) {
    auto packet = session.packet();
    if (packet[0] == type) return parse_event(packet, session.order);
  }
  throw std::runtime_error("expected input event was not delivered");
}
}  // namespace

int main(int argc, char** argv) try {
  Options options;
  if (!parse(argc, argv, options)) {
    std::cerr << "Usage: x11_milestone8_probe --display :N --input-socket PATH "
                 "--byte-order little|big --scenario "
                 "routing|propagation|state|errors\n";
    return 2;
  }
  Session x11_client(options.socket_dir + "/X" + options.display, options.order);
  gw::test::SyntheticInputClient input(options.input_socket);
  const std::uint32_t window = x11_client.base + 1;
  constexpr std::uint32_t cw_back_pixel = 1U << 1U;
  constexpr std::uint32_t cw_event_mask = 1U << 11U;
  constexpr std::uint32_t selected =
      x11::event_mask::KeyPress | x11::event_mask::KeyRelease |
      x11::event_mask::ButtonPress | x11::event_mask::ButtonRelease |
      x11::event_mask::EnterWindow | x11::event_mask::LeaveWindow |
      x11::event_mask::PointerMotion | x11::event_mask::Button1Motion |
      x11::event_mask::Exposure | x11::event_mask::StructureNotify |
      x11::event_mask::FocusChange;
  const std::uint32_t values[] = {0x00204080U, selected};
  x11_client.send(x11_client.wire.create_window(
      window, x11_client.root, 40, 40, 240, 160,
      cw_back_pixel | cw_event_mask, values));
  x11_client.send(map_request(x11_client.wire, window, options.order));
  x11_client.sync();

  std::vector<Event> events;
  auto add = [&](Event event) { events.push_back(event); };
  if (options.scenario == "routing" || options.scenario == "propagation") {
    const auto motion_ack = input.motion(1, 2, 80, 80);
    require(motion_ack.result == GWIPC_SYNTHETIC_INPUT_ACCEPTED,
            "motion was not accepted");
    add(wait_for(x11_client, 7));
    add(wait_for(x11_client, 6));
    require(events.back().time == 2 && events.back().event == window,
            "motion target or time mismatch");
    const auto press_ack = input.button(2, 3, 1, true);
    require(press_ack.result == GWIPC_SYNTHETIC_INPUT_ACCEPTED ||
                press_ack.result == GWIPC_SYNTHETIC_INPUT_FOCUS_UNCHANGED,
            "button press was not accepted");
    add(wait_for(x11_client, 4));
    const auto release_ack = input.button(3, 4, 1, false);
    require(release_ack.result == GWIPC_SYNTHETIC_INPUT_ACCEPTED,
            "button release was not accepted");
    add(wait_for(x11_client, 5));
    (void)input.barrier(4);
  } else if (options.scenario == "state") {
    (void)input.motion(1, 2, 80, 80);
    add(wait_for(x11_client, 7)); add(wait_for(x11_client, 6));
    (void)input.key(2, 3, 50, true); add(wait_for(x11_client, 2));
    (void)input.key(3, 4, 38, true); add(wait_for(x11_client, 2));
    require(events.back().state == x11::state_mask::Shift,
            "shift state was not present on key event");
    (void)input.key(4, 5, 38, false); add(wait_for(x11_client, 3));
    (void)input.key(5, 6, 50, false); add(wait_for(x11_client, 3));
  } else {
    const auto invalid = input.button(1, 2, 1, false);
    require(invalid.result == GWIPC_SYNTHETIC_INPUT_INVALID_TRANSITION &&
                invalid.delivered_event_count == 0,
            "invalid transition acknowledgement mismatch");
    x11_client.sync();
    (void)input.barrier(2);
  }

  std::cout << "{\"byte_order\":\""
            << (options.order == x11::ByteOrder::LittleEndian ? "little" : "big")
            << "\",\"scenario\":\"" << options.scenario
            << "\",\"events\":[";
  for (std::size_t index = 0; index < events.size(); ++index) {
    if (index != 0) std::cout << ',';
    const auto& event = events[index];
    std::cout << "{\"type\":" << static_cast<unsigned>(event.type)
              << ",\"detail\":" << static_cast<unsigned>(event.detail)
              << ",\"time\":" << event.time << ",\"event\":"
              << event.event << ",\"state\":" << event.state
              << ",\"sequence\":" << event.sequence << '}';
  }
  std::cout << "]}\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << "x11_milestone8_probe: " << error.what() << '\n';
  return 1;
}
