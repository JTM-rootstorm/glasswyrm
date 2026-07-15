#include "glasswyrmd/event_router.hpp"
#include "glasswyrmd/real_input_controller.hpp"
#include "glasswyrmd/server_state.hpp"
#include "input/fake_libinput_api.hpp"
#include "input/input_router.hpp"
#include "protocol/x11/event_mask.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <cstdint>
#include <fcntl.h>
#include <memory>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace input = glasswyrm::input;
namespace server = glasswyrm::server;
namespace x11 = gw::protocol::x11;
using gw::test::require;

namespace {

struct ClientPair {
  int peer{-1};
  server::ClientConnection connection;

  ClientPair(const std::uint64_t id, const std::uint32_t base,
             server::ServerState &state)
      : connection(make_pair(peer), id, base, state) {}

  ~ClientPair() {
    if (peer >= 0)
      ::close(peer);
  }

  static int make_pair(int &peer) {
    int sockets[2];
    require(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) == 0,
            "create client connection socket pair");
    peer = sockets[1];
    const int flags = ::fcntl(sockets[0], F_GETFL, 0);
    require(flags >= 0 && ::fcntl(sockets[0], F_SETFL, flags | O_NONBLOCK) == 0,
            "make server client connection nonblocking");
    return sockets[0];
  }
};

void complete_setup(ClientPair &client) {
  constexpr std::array<std::uint8_t, 12> setup{'l', 0, 11, 0, 0, 0,
                                               0,   0, 0,  0, 0, 0};
  require(::send(client.peer, setup.data(), setup.size(), 0) ==
              static_cast<ssize_t>(setup.size()),
          "send little-endian X11 setup");
  client.connection.handle_events(POLLIN);
  client.connection.handle_events(POLLOUT);
  std::array<std::uint8_t, 8192> reply{};
  require(::recv(client.peer, reply.data(), reply.size(), 0) > 0,
          "receive X11 setup reply");
}

input::LibinputEvent device(const std::uint64_t id,
                            const input::DeviceCapability capability) {
  input::LibinputEvent result;
  result.kind = input::LibinputEventKind::DeviceAdded;
  result.device_id = id;
  result.capabilities = static_cast<std::uint8_t>(capability);
  return result;
}

std::uint16_t read_u16(const std::array<std::uint8_t, 32> &packet,
                       const std::size_t offset) {
  return static_cast<std::uint16_t>(packet[offset]) |
         static_cast<std::uint16_t>(packet[offset + 1] << 8U);
}

std::uint32_t read_u32(const std::array<std::uint8_t, 32> &packet,
                       const std::size_t offset) {
  return static_cast<std::uint32_t>(packet[offset]) |
         (static_cast<std::uint32_t>(packet[offset + 1]) << 8U) |
         (static_cast<std::uint32_t>(packet[offset + 2]) << 16U) |
         (static_cast<std::uint32_t>(packet[offset + 3]) << 24U);
}

} // namespace

int main() {
  constexpr std::uint32_t root_width = 100;
  constexpr std::uint32_t root_height = 80;
  constexpr std::uint64_t client_id = 1;
  constexpr std::uint32_t resource_base = 0x00200000U;

  auto api = std::make_unique<input::FakeLibinputApi>();
  auto *fixture = api.get();
  fixture->queue(device(1, input::DeviceCapabilityKeyboard));
  fixture->queue(device(2, input::DeviceCapabilityAbsolutePointer));

  server::RealInputControllerConfig config;
  config.device_paths = {"/dev/null"};
  config.root_width = root_width;
  config.root_height = root_height;
  std::string error;
  auto controller =
      server::RealInputController::create(std::move(api), config, error);
  require(controller && controller->ready(), error);

  auto screen = server::kScreenModel;
  screen.width_pixels = root_width;
  screen.height_pixels = root_height;
  server::ServerState state(screen);
  ClientPair client(client_id, resource_base, state);
  complete_setup(client);

  server::WindowCreateSpec window;
  window.xid = resource_base + 1;
  window.parent = state.screen().root_window;
  window.width = root_width;
  window.height = root_height;
  window.depth = state.screen().root_depth;
  window.window_class = server::WindowClass::InputOutput;
  window.visual = state.screen().root_visual;
  require(state.resources().create_window(
              client_id, resource_base, state.screen().resource_id_mask,
              window) == server::CreateWindowStatus::Success,
          "create full-screen input target");
  auto *target = state.resources().find_window(window.xid);
  target->map_requested = true;
  target->map_state = server::MapState::Viewable;
  target->policy_visible = true;
  require(state.resources().set_event_selection(window.xid, client_id,
                                                x11::event_mask::PointerMotion),
          "select pointer motion on input target");

  input::LibinputEvent absolute;
  absolute.kind = input::LibinputEventKind::MotionAbsolute;
  absolute.device_id = 2;
  absolute.time_usec = 7000;
  absolute.x = 500.0;
  absolute.y = -10.0;
  fixture->queue(absolute);
  const auto serviced = controller->service_backend(window.xid);
  const auto routed = controller->take_event();
  require(serviced.success && routed &&
              routed->kind == server::RealInputEventKind::Motion &&
              routed->root_x == 99 && routed->root_y == 0,
          "absolute libinput event reaches the controller with bounded root "
          "coordinates");

  input::InputState input_state;
  const auto old_target = input_state.pointer_target();
  const auto new_target = input::hit_test_top_level(
      state.resources(), routed->root_x, routed->root_y);
  require(new_target == window.xid,
          "bounded position hits the full-screen target");
  require(input_state.accept_wrapping_time(routed->time_ms),
          "accept absolute motion timestamp");
  input_state.set_pointer(routed->root_x, routed->root_y, new_target);

  std::array<server::ClientConnection *, 1> clients{&client.connection};
  const server::EventRouter router(state.resources());
  require(router.route_crossing(old_target, new_target, state.focused_window(),
                                input_state, clients) == 0,
          "unselected crossing does not add a wire event");
  require(router.route_input_grabbed(
              state.grabs(), x11::CoreEventType::MotionNotify, 0,
              routed->time_ms, new_target, routed->state_before,
              input::motion_delivery_mask(input_state), routed->root_x,
              routed->root_y, new_target, clients) == 1,
          "server routes absolute motion to the selected X11 client");

  client.connection.handle_events(POLLOUT);
  std::array<std::uint8_t, 32> packet{};
  require(::recv(client.peer, packet.data(), packet.size(), MSG_WAITALL) ==
              static_cast<ssize_t>(packet.size()),
          "receive routed MotionNotify packet");
  require(packet[0] ==
                  static_cast<std::uint8_t>(x11::CoreEventType::MotionNotify) &&
              read_u32(packet, 12) == window.xid &&
              static_cast<std::int16_t>(read_u16(packet, 20)) == 99 &&
              static_cast<std::int16_t>(read_u16(packet, 22)) == 0 &&
              static_cast<std::int16_t>(read_u16(packet, 24)) == 99 &&
              static_cast<std::int16_t>(read_u16(packet, 26)) == 0,
          "MotionNotify preserves bounded screen and target coordinates");
  return 0;
}
