#include "glasswyrmd/event_router.hpp"
#include "glasswyrmd/server_state.hpp"
#include "protocol/x11/event_mask.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <poll.h>
#include <span>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace {
using glasswyrm::server::ClientConnection;
using gw::test::require;

struct ClientPair {
  int peer{-1};
  ClientConnection connection;
  ClientPair(const std::uint64_t id, const std::uint32_t base,
             glasswyrm::server::ServerState &state)
      : connection(make_pair(peer), id, base, state) {}
  ~ClientPair() {
    if (peer >= 0)
      ::close(peer);
  }

  static int make_pair(int &peer) {
    int sockets[2];
    require(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) == 0,
            "create connection socket pair");
    peer = sockets[1];
    const int flags = ::fcntl(sockets[0], F_GETFL, 0);
    require(flags >= 0 && ::fcntl(sockets[0], F_SETFL, flags | O_NONBLOCK) == 0,
            "make server connection nonblocking");
    return sockets[0];
  }
};

void send_setup(ClientPair &pair, const bool big_endian) {
  require(pair.peer >= 0, "setup peer descriptor is valid");
  const std::array<std::uint8_t, 12> little{'l', 0, 11, 0, 0, 0,
                                            0,   0, 0,  0, 0, 0};
  const std::array<std::uint8_t, 12> big{'B', 0, 0, 11, 0, 0, 0, 0, 0, 0, 0, 0};
  const auto &setup = big_endian ? big : little;
  require(::send(pair.peer, setup.data(), setup.size(), 0) == 12, "send setup");
  pair.connection.handle_events(POLLIN);
  pair.connection.handle_events(POLLOUT);
  std::array<std::uint8_t, 8192> discarded{};
  require(::recv(pair.peer, discarded.data(), discarded.size(), 0) > 0,
          "drain setup reply");
}

void send_focus(ClientPair &pair, const bool big_endian) {
  const std::array<std::uint8_t, 4> little{43, 0, 1, 0};
  const std::array<std::uint8_t, 4> big{43, 0, 0, 1};
  const auto &request = big_endian ? big : little;
  require(::send(pair.peer, request.data(), request.size(), 0) == 4,
          "send focus request");
  pair.connection.handle_events(POLLIN);
  pair.connection.handle_events(POLLOUT);
  std::array<std::uint8_t, 32> reply{};
  require(::recv(pair.peer, reply.data(), reply.size(), MSG_WAITALL) == 32,
          "receive focus reply");
}

std::array<std::uint8_t, 32> receive_event(ClientPair &pair) {
  pair.connection.handle_events(POLLOUT);
  std::array<std::uint8_t, 32> event{};
  require(::recv(pair.peer, event.data(), event.size(), MSG_WAITALL) == 32,
          "receive routed event");
  return event;
}

} // namespace

int main() {
  glasswyrm::server::ServerState state;
  ClientPair target_client(1, 0x00200000U, state);
  ClientPair parent_client(2, 0x00400000U, state);
  send_setup(target_client, false);
  send_setup(parent_client, true);

  target_client.connection.set_dispatch_blocked(77);
  const std::array<std::uint8_t, 4> focus{43, 0, 1, 0};
  require(::send(target_client.peer, focus.data(), focus.size(), 0) == 4,
          "queue request behind barrier");
  target_client.connection.handle_events(POLLIN);
  require(target_client.connection.last_request_sequence() == 0 &&
              !target_client.connection.clear_dispatch_blocked(78) &&
              target_client.connection.dispatch_blocked(),
          "wrong token preserves dispatch barrier");
  require(target_client.connection.clear_dispatch_blocked(77),
          "matching token clears dispatch barrier");
  target_client.connection.handle_events(POLLIN);
  target_client.connection.handle_events(POLLOUT);
  std::array<std::uint8_t, 32> reply{};
  require(::recv(target_client.peer, reply.data(), reply.size(), MSG_WAITALL) ==
                  32 &&
              target_client.connection.last_request_sequence() == 1,
          "barrier resume dispatches once without a sequence increment");
  send_focus(parent_client, true);
  send_focus(parent_client, true);

  glasswyrm::server::WindowCreateSpec spec;
  spec.xid = 0x00200001U;
  spec.parent = state.screen().root_window;
  spec.width = 100;
  spec.height = 80;
  spec.depth = 24;
  spec.window_class = glasswyrm::server::WindowClass::InputOutput;
  spec.visual = state.screen().root_visual;
  require(state.resources().create_window(
              1, 0x00200000U, state.screen().resource_id_mask, spec) ==
              glasswyrm::server::CreateWindowStatus::Success,
          "create routed window");
  require(state.resources().set_event_selection(
              spec.xid, 1,
              (1U << 17U) | gw::protocol::x11::event_mask::FocusChange |
                  gw::protocol::x11::event_mask::LeaveWindow |
                  gw::protocol::x11::event_mask::ButtonPress |
                  gw::protocol::x11::event_mask::ButtonRelease) &&
              state.resources().set_event_selection(state.screen().root_window,
                  2, (1U << 19U) |
                         gw::protocol::x11::event_mask::FocusChange |
                         gw::protocol::x11::event_mask::EnterWindow),
          "install per-client structural selections");

  std::array<ClientConnection *, 2> clients{&target_client.connection,
                                            &parent_client.connection};
  const glasswyrm::server::EventRouter router(state.resources());
  const auto before_map = router.capture(spec.xid);
  state.resources().find_window(spec.xid)->map_requested = true;
  state.resources().find_window(spec.xid)->map_state =
      glasswyrm::server::MapState::Viewable;
  const auto after_map = router.capture(spec.xid);
  require(
      router.route_transition(glasswyrm::server::StructuralTransitionKind::Map,
                              before_map, after_map, clients) == 2,
      "actual map transition routes exactly once per selection");
  require(receive_event(target_client)[0] == 19 &&
              receive_event(parent_client)[0] == 19,
          "map transition emits MapNotify");
  require(
      router.route_transition(glasswyrm::server::StructuralTransitionKind::Map,
                              after_map, after_map, clients) == 0,
      "no-op map emits no event");

  glasswyrm::server::WindowCreateSpec exposed_child = spec;
  exposed_child.xid = 0x00200003U;
  exposed_child.parent = spec.xid;
  exposed_child.width = 20;
  exposed_child.height = 10;
  require(state.resources().create_window(
              1, 0x00200000U, state.screen().resource_id_mask,
              exposed_child) == glasswyrm::server::CreateWindowStatus::Success,
          "create exposed child");
  auto* child_window = state.resources().find_window(exposed_child.xid);
  child_window->map_requested = true;
  state.resources().recompute_map_states(exposed_child.xid);
  require(state.resources().set_event_selection(
              exposed_child.xid, 1,
              gw::protocol::x11::event_mask::Exposure),
          "select child exposure");
  require(router.route_viewable_subtree_expose(spec.xid, clients) == 1,
          "mapped ancestor exposes its viewable child subtree");
  const auto child_expose = receive_event(target_client);
  require(child_expose[0] == 12 && child_expose[12] == 20 &&
              child_expose[14] == 10,
          "child receives full drawable exposure");

  glasswyrm::server::WindowCreateSpec sibling_spec = spec;
  sibling_spec.xid = 0x00200002U;
  require(state.resources().create_window(
              1, 0x00200000U, state.screen().resource_id_mask, sibling_spec) ==
              glasswyrm::server::CreateWindowStatus::Success,
          "create committed above sibling");
  const auto before_configure = router.capture(spec.xid);
  state.resources().find_window(spec.xid)->x = 40;
  const auto after_configure = router.capture(spec.xid);
  require(after_configure && after_configure->above_sibling == sibling_spec.xid,
          "committed root order derives above sibling");
  require(router.route_transition(
              glasswyrm::server::StructuralTransitionKind::Configure,
              before_configure, after_configure, clients) == 2,
          "actual configure transition routes exactly once per selection");
  const auto target_configure = receive_event(target_client);
  const auto parent_configure = receive_event(parent_client);
  require(target_configure[0] == 22 && target_configure[12] == 2 &&
              target_configure[13] == 0 && target_configure[14] == 32 &&
              target_configure[15] == 0 && parent_configure[0] == 22 &&
              parent_configure[12] == 0 && parent_configure[13] == 32 &&
              parent_configure[14] == 0 && parent_configure[15] == 2,
          "ConfigureNotify encodes committed above sibling in recipient order");
  require(router.route_transition(
              glasswyrm::server::StructuralTransitionKind::Configure,
              after_configure, after_configure, clients) == 0,
          "no-op configure emits no event");
  require(router.route_transition(
              glasswyrm::server::StructuralTransitionKind::Destroy,
              after_configure, std::nullopt, clients) == 2,
          "captured recipients survive target destruction");
  require(receive_event(target_client)[0] == 17 &&
              receive_event(parent_client)[0] == 17,
          "captured destroy transition emits DestroyNotify");

  require(router.route_map(spec.xid, state.screen().root_window, true,
                           clients) == 2,
          "route target and parent selections exactly once");
  const auto target_event = receive_event(target_client);
  const auto parent_event = receive_event(parent_client);
  require(
      target_event[0] == 19 && target_event[2] == 1 && target_event[3] == 0 &&
          target_event[4] == 1 && parent_event[0] == 19 &&
          parent_event[2] == 0 && parent_event[3] == 2 &&
          parent_event[4] == 0 && parent_event[7] == 1,
      "events use recipient sequence, byte order, and selected event field");

  const auto grab_recipient = router.input_recipient(
      spec.xid, gw::protocol::x11::event_mask::ButtonPress);
  require(grab_recipient && grab_recipient->first == 1 &&
              state.grabs().begin_automatic_button_grab(
                  grab_recipient->first, grab_recipient->second, 1, 50),
          "automatic grab captures the ButtonPress recipient");
  state.grabs().note_button_press(1);
  require(router.route_input_grabbed(
              state.grabs(), gw::protocol::x11::CoreEventType::ButtonRelease,
              1, 51, state.screen().root_window, 0,
              gw::protocol::x11::event_mask::ButtonRelease, 5, 6,
              state.screen().root_window, clients) == 1,
          "automatic grab redirects release away from the natural target");
  const auto grabbed_release = receive_event(target_client);
  require(grabbed_release[0] == 5 && grabbed_release[4] == 51 &&
              state.grabs().note_button_release(1),
          "redirected release is encoded and ends the automatic grab");

  glasswyrm::input::InputState input;
  input.set_pointer(52, 33, spec.xid);
  const auto input_before = router.capture_input_transition(spec.xid, spec.xid);
  input.set_pointer(52, 33, state.screen().root_window);
  require(router.route_lifecycle_input_transition(
              input_before, state.screen().root_window,
              state.screen().root_window, input, clients) == 4,
          "lifecycle input transition routes captured departures and committed arrivals");
  const auto focus_out = receive_event(target_client);
  const auto leave = receive_event(target_client);
  const auto focus_in = receive_event(parent_client);
  const auto enter = receive_event(parent_client);
  require(focus_out[0] == 10 && leave[0] == 8 &&
              focus_in[0] == 9 && enter[0] == 7,
          "lifecycle input transition preserves focus-before-crossing order");

  require(!target_client.connection.enqueue_server_packet(
              std::vector<std::uint8_t>(1024U * 1024U + 1U)),
          "oversized event closes only its recipient");
  require(router.route_unmap(spec.xid, state.screen().root_window, clients) ==
              1,
          "closed output recipient does not block surviving delivery");
  const auto isolated = receive_event(parent_client);
  require(isolated[0] == 18, "surviving recipient receives the event");
  return 0;
}
