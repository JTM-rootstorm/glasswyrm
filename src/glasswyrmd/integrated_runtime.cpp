#include "glasswyrmd/server_runtime.hpp"

#ifdef GW_SERVER_HAS_IPC
#include "glasswyrmd/screen_geometry.hpp"
#include "input/input_router.hpp"
#include "protocol/x11/event_mask.hpp"

#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <poll.h>
#endif

namespace glasswyrm::server {

#ifdef GW_SERVER_HAS_IPC
bool ServerRuntime::warp_pointer(const std::int32_t x,
                                 const std::int32_t y) {
  const auto old_target = input_state_.pointer_target();
  const auto new_target = glasswyrm::input::hit_test_deepest_viewable(
      server_.state_.resources(), x, y);
  input_state_.advance_time();
  input_state_.set_pointer(x, y, new_target);
#if GW_HAS_LIBINPUT_BACKEND
  if (real_input_)
    real_input_->warp_pointer(x, y);
#endif
  cursor_dirty_ = true;

  std::vector<ClientConnection*> clients;
  clients.reserve(server_.clients_.size());
  for (const auto& client : server_.clients_)
    clients.push_back(client.get());
  EventRouter router(server_.state_.resources());
  (void)router.route_crossing(old_target, new_target,
                              server_.state_.focused_window(), input_state_,
                              clients);
  (void)router.route_input_grabbed(
      server_.state_.grabs(), gw::protocol::x11::CoreEventType::MotionNotify,
      0, input_state_.time(), new_target, input_state_.mask(),
      glasswyrm::input::motion_delivery_mask(input_state_), x, y, new_target,
      clients);
  return true;
}
#endif

int ServerRuntime::initialize_integrated(SignalRuntime& signals) {
#ifdef GW_SERVER_HAS_IPC
  server_.input_snapshot_provider_ = [this] {
    InputSnapshot snapshot;
    snapshot.root_x = input_state_.pointer_x();
    snapshot.root_y = input_state_.pointer_y();
    snapshot.state_mask = input_state_.mask();
    snapshot.pointer_target = input_state_.pointer_target();
    snapshot.logical_time = input_state_.time();
    snapshot.keymap = input_state_.query_keymap();
    snapshot.warp_pointer = [this](const std::int32_t x,
                                   const std::int32_t y) {
      return warp_pointer(x, y);
    };
#if GW_HAS_LIBINPUT_BACKEND
    if (real_input_) {
      snapshot.keyboard_mapping = real_input_->keyboard_mapping();
      snapshot.set_global_auto_repeat = [this](const bool enabled) {
        return real_input_ && real_input_->set_global_auto_repeat(enabled);
      };
    }
#endif
    return snapshot;
  };
  if (!server_.options_.integrated()) return 0;

  bridge_ = std::make_unique<RuntimeBridge>(
      *server_.options_.wm_socket, *server_.options_.compositor_socket,
      server_.state_.screen(), std::chrono::seconds(10),
      server_.options_.software_content, server_.options_.real_input_enabled(),
      server_.options_.game_compat, server_.options_.output_model);
  if (server_.options_.software_content) {
    content_presenter_ = std::make_unique<ContentPresenter>(
        server_.options_.game_compat ? GWIPC_SYNCHRONIZATION_EVENTFD
                                    : GWIPC_SYNCHRONIZATION_NONE);
    if (server_.options_.real_input_enabled() ||
        (server_.options_.output_model &&
         server_.options_.synthetic_input_socket)) {
      cursor_presenter_ = std::make_unique<CursorPresenter>();
      cursor_dirty_ = server_.options_.real_input_enabled();
    }
    server_.drawable_damage_handler_ = [this](
        const std::vector<DrawableDamage>& damage) {
      for (const auto& item : damage)
        if (item.buffer_rectangle)
          content_presenter_->damage_scaled(item.window, item.rectangle,
                                            *item.buffer_rectangle);
        else
          content_presenter_->damage(item.window, item.rectangle);
    };
  }
  bridge_->start();
  while (!SignalRuntime::stop_requested() && !bridge_->ready()) {
    pollfd bootstrap[3] = {
        {signals.read_descriptor(), POLLIN, 0},
        {bridge_->policy_fd(), bridge_->policy_events(), 0},
        {bridge_->compositor_fd(), bridge_->compositor_events(), 0}};
    const int count = ::poll(
        bootstrap, 3, bridge_->poll_timeout_ms(RuntimeBridge::Clock::now()));
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) {
      std::fprintf(stderr,
                   "glasswyrmd: integrated bootstrap poll failed: %s\n",
                   std::strerror(errno));
      return 1;
    }
    if ((bootstrap[0].revents & POLLIN) != 0) {
      SignalRuntime::request_stop();
      break;
    }
    std::string error;
    if (!bridge_->service(bootstrap[1].revents, bootstrap[2].revents,
                          RuntimeBridge::Clock::now(), error)) {
      std::fprintf(stderr, "glasswyrmd: integrated bootstrap failed: %s\n",
                   error.c_str());
      return 1;
    }
  }
  if (SignalRuntime::stop_requested()) return 2;

  if (server_.options_.output_model) {
    const auto *layout = bridge_->output_layout();
    const auto screen = layout ? derive_output_screen_model(*layout)
                               : std::nullopt;
    if (!screen || !server_.state_.update_screen_geometry(*screen) ||
        !server_.state_.randr().configure_output_layout(*layout)) {
      std::fprintf(stderr,
                   "glasswyrmd: compositor output inventory could not "
                   "initialize the server screen\n");
      return 1;
    }
    if (server_.options_.control_socket) {
      output_control_peer_ = std::make_unique<OutputControlPeer>(
          *server_.options_.control_socket, *layout,
          [this] { return server_.state_.lifecycle_snapshot(); });
      std::string error;
      if (!output_control_peer_->start(error)) {
        std::fprintf(stderr, "glasswyrmd: %s\n", error.c_str());
        return 1;
      }
    }
  }

  initialize_lifecycle();
#if GW_HAS_LIBINPUT_BACKEND
  if (server_.options_.real_input_enabled() &&
      (!initialize_interactive_policy() || !initialize_real_input()))
      return 1;
#endif
  if (server_.options_.synthetic_input_socket) {
    input_peer_ = std::make_unique<SyntheticInputPeer>(
        *server_.options_.synthetic_input_socket);
    std::string error;
    if (!input_peer_->start(error)) {
      std::fprintf(stderr, "glasswyrmd: %s\n", error.c_str());
      return 1;
    }
  }
  return 0;
#else
  static_cast<void>(signals);
  if (server_.options_.integrated()) {
    std::fprintf(stderr,
                 "glasswyrmd: integrated mode requires libgwipc support\n");
    return 1;
  }
  return 0;
#endif
}

#ifdef GW_SERVER_HAS_IPC

bool ServerRuntime::service_integrated(const short policy_events,
                                       const short compositor_events) {
  bool compositor_reset = false;
  if (!service_peer_readiness(policy_events, compositor_events,
                              compositor_reset))
    return false;
  std::vector<CompositorBufferRelease> compositor_releases;
  if (!service_input_session_work(compositor_reset, compositor_releases))
    return false;
  std::string error;
  if (!service_peer_replay(error) || !service_output_control_work() ||
      !service_lifecycle_work(compositor_releases) ||
      !service_output_control_work())
    return false;
  if (!service_cursor()) return false;
  submit_pending_content(error);
  return true;
}

#endif

}  // namespace glasswyrm::server
