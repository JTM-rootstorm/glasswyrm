#include "glasswyrmd/server_runtime.hpp"

#include "input/input_router.hpp"
#include "protocol/x11/event_mask.hpp"

#include <cstdio>

namespace glasswyrm::server {
namespace {

std::vector<ClientConnection *>
recipients(const std::vector<std::unique_ptr<ClientConnection>> &source) {
  std::vector<ClientConnection *> result;
  result.reserve(source.size());
  for (const auto &client : source)
    result.push_back(client.get());
  return result;
}

} // namespace

bool ServerRuntime::initialize_real_input() {
  std::string error;
  if (!validate_input_device_paths(server_.options_, error)) {
    std::fprintf(stderr,
                 "glasswyrmd: real input device validation failed: %s\n",
                 error.c_str());
    return false;
  }
  RealInputControllerConfig config;
  config.device_paths.reserve(server_.options_.libinput_devices.size());
  for (const auto &device : server_.options_.libinput_devices)
    config.device_paths.push_back(device.canonical_path);
  config.keymap.rules = server_.options_.xkb_rules;
  config.keymap.model = server_.options_.xkb_model;
  config.keymap.layout = server_.options_.xkb_layout;
  config.keymap.variant = server_.options_.xkb_variant;
  config.keymap.options = server_.options_.xkb_options;
  config.repeat.delay_ms = server_.options_.repeat_delay_ms;
  config.repeat.rate_hz = server_.options_.repeat_rate_hz;
  config.root_width = server_.state_.screen().width_pixels;
  config.root_height = server_.state_.screen().height_pixels;
  real_input_ = RealInputController::create(
      glasswyrm::input::make_real_libinput_api(), std::move(config), error);
  if (!real_input_) {
    std::fprintf(stderr, "glasswyrmd: real input initialization failed: %s\n",
                 error.c_str());
    return false;
  }
  std::fprintf(
      stderr, "glasswyrmd: real input ready devices=%zu keyboard=1 pointer=1\n",
      server_.options_.libinput_devices.size());
  mark_cursor_dirty();
  return true;
}

bool ServerRuntime::service_cursor() {
  if (!cursor_presenter_ || !real_input_ || !lifecycle_ || !bridge_->ready())
    return true;
  const auto image = current_cursor_image();
  if (!image) {
    std::fprintf(stderr, "glasswyrmd: effective cursor image is unavailable\n");
    return false;
  }
  const bool visible = real_input_->active();
  const bool interactive_override =
      visible && interactive_policy_ &&
      ((interactive_policy_->cursor() ==
            glasswyrm::wm::InteractionCursor::FleurMove &&
        image == move_cursor_) ||
       (interactive_policy_->cursor() ==
            glasswyrm::wm::InteractionCursor::BottomRightResize &&
        image == resize_cursor_));
  if (!cursor_dirty_ &&
      !cursor_presenter_->needs_update(
          image, input_state_.pointer_x(), input_state_.pointer_y(), visible))
    return true;
  if (lifecycle_->phase() != CoordinatorPhase::Idle ||
      !bridge_->transaction_idle() || cursor_presenter_->in_flight() ||
      (content_presenter_ && content_presenter_->frame_in_flight()))
    return true;
  if (!cursor_presenter_->needs_update(
          image, input_state_.pointer_x(), input_state_.pointer_y(), visible)) {
    cursor_dirty_ = false;
    cursor_force_buffer_ = false;
    if (interactive_override)
      complete_interactive_cursor_publication();
    return true;
  }
  CompositorCursorSubmission submission;
  std::string error;
  if (!cursor_presenter_->prepare(
          image, input_state_.pointer_x(), input_state_.pointer_y(), visible,
          submission, error, cursor_force_buffer_)) {
    std::fprintf(stderr, "glasswyrmd: cursor preparation failed: %s\n",
                 error.c_str());
    return false;
  }
  if (!bridge_->submit_cursor(submission, next_compositor_commit_++,
                              next_compositor_generation_++, error)) {
    cursor_presenter_->reject();
    std::fprintf(stderr, "glasswyrmd: cursor submission failed: %s\n",
                 error.c_str());
    return false;
  }
  cursor_dirty_ = false;
  cursor_force_buffer_ = false;
  cursor_submission_interactive_ = interactive_override;
  cursor_submission_diagnostic_ = PendingCursorDiagnostic{
      image->kind, submission.surface.logical_x, submission.surface.logical_y,
      submission.surface.visible != 0, submission.buffer.has_value()};
  return true;
}

bool ServerRuntime::service_real_input(const short input_events,
                                       const short repeat_events) {
  constexpr short fatal_events = POLLERR | POLLHUP | POLLNVAL;
  if ((input_events & fatal_events) != 0) {
    std::fprintf(stderr, "glasswyrmd: real input poll descriptor failed\n");
    return false;
  }
  if ((repeat_events & fatal_events) != 0) {
    std::fprintf(stderr, "glasswyrmd: repeat timer poll descriptor failed\n");
    return false;
  }
  if ((input_events & POLLIN) != 0 || real_input_->backend_work_pending()) {
    const auto serviced =
        real_input_->service_backend(server_.state_.focused_window());
    if (!serviced.success) {
      std::fprintf(stderr, "glasswyrmd: real input service failed: %s\n",
                   serviced.error.c_str());
      return false;
    }
    if (serviced.input_unavailable)
      std::fprintf(stderr,
                   "glasswyrmd: required real input capability unavailable\n");
  }
  if ((repeat_events & POLLIN) != 0) {
    const auto serviced = real_input_->service_repeat();
    if (!serviced.success) {
      std::fprintf(stderr, "glasswyrmd: key repeat service failed: %s\n",
                   serviced.error.c_str());
      return false;
    }
  }
  deliver_real_input();
  return true;
}

bool ServerRuntime::service_session_changes() {
  if (!real_input_)
    return true;
  for (const auto &request : bridge_->take_session_state_changes()) {
    const auto applied = real_input_->apply_session_state(request.change.state);
    mark_cursor_dirty();
    if (applied.reset_server_state) {
      input_state_.reset_provider_state();
      abort_interactive();
      (void)server_.state_.grabs().suspend();
    }
    std::string error;
    if (!bridge_->acknowledge_session_state(request, applied.result, error)) {
      std::fprintf(stderr,
                   "glasswyrmd: session state acknowledgement failed: %s\n",
                   error.c_str());
      return false;
    }
    std::fprintf(
        stderr,
        "glasswyrmd: session state generation=%llu state=%u result=%u\n",
        static_cast<unsigned long long>(request.change.generation),
        static_cast<unsigned>(request.change.state),
        static_cast<unsigned>(applied.result));
    if (applied.fatal) {
      std::fprintf(stderr, "glasswyrmd: fatal real input resume failure: %s\n",
                   applied.error.c_str());
      return false;
    }
  }
  return true;
}

bool ServerRuntime::suspend_real_input_for_compositor_reset(const bool reset) {
  if (!reset || !real_input_)
    return true;
  real_input_suspended_for_compositor_reset_ = true;
  if (!real_input_->active()) return true;
  const auto suspended =
      real_input_->apply_session_state(GWIPC_SESSION_INACTIVE);
  mark_cursor_dirty();
  if (suspended.reset_server_state)
    input_state_.reset_provider_state();
  abort_interactive();
  (void)server_.state_.grabs().suspend();
  if (suspended.result == GWIPC_SESSION_STATE_ACCEPTED ||
      suspended.result == GWIPC_SESSION_STATE_ALREADY_APPLIED)
    return true;
  std::fprintf(stderr,
               "glasswyrmd: could not suspend real input after compositor "
               "disconnect: %s\n",
               suspended.error.c_str());
  return false;
}

bool ServerRuntime::resume_real_input_after_compositor_reset() {
  if (!real_input_suspended_for_compositor_reset_ || !bridge_->ready())
    return true;
  const auto resumed =
      real_input_->apply_session_state(GWIPC_SESSION_ACTIVE);
  mark_cursor_dirty();
  if (resumed.reset_server_state) {
    input_state_.reset_provider_state();
    abort_interactive();
    (void)server_.state_.grabs().suspend();
  }
  if (resumed.result == GWIPC_SESSION_STATE_ACCEPTED ||
      resumed.result == GWIPC_SESSION_STATE_ALREADY_APPLIED) {
    real_input_suspended_for_compositor_reset_ = false;
    return true;
  }
  std::fprintf(stderr,
               "glasswyrmd: could not resume real input after compositor "
               "reconnect: %s\n",
               resumed.error.c_str());
  return false;
}

void ServerRuntime::complete_real_focus(const bool success) {
  const auto new_focus = server_.state_.focused_window();
  if (success) {
    auto clients = recipients(server_.clients_);
    EventRouter router(server_.state_.resources());
    (void)router.route_focus(pending_real_focus_->old_focus, new_focus,
                             clients);
  }
  pending_real_focus_.reset();
}

void ServerRuntime::deliver_real_input() {
  std::size_t processed = 0;
  while (!pending_real_focus_ && processed++ < 64) {
    const auto next = real_input_->take_event();
    if (!next)
      break;
    const auto event = *next;
    mark_cursor_dirty();
    if (event.kind == RealInputEventKind::StateReset) {
      abort_interactive();
      (void)server_.state_.grabs().suspend();
      input_state_.reset_provider_state();
      continue;
    }
    auto clients = recipients(server_.clients_);
    EventRouter router(server_.state_.resources());
    if (event.kind == RealInputEventKind::Motion) {
      const auto old_target = input_state_.pointer_target();
      const auto new_target = glasswyrm::input::hit_test_top_level(
          server_.state_.resources(), event.root_x, event.root_y);
      (void)input_state_.accept_wrapping_time(event.time_ms);
      input_state_.set_pointer(event.root_x, event.root_y, new_target);
      if (interactive_policy_ &&
          interactive_policy_->kind() !=
              glasswyrm::wm::InteractionKind::None) {
        interactive_policy_->motion({event.root_x, event.root_y});
        (void)update_interactive_geometry();
        continue;
      }
      (void)router.route_crossing(old_target, new_target,
                                  server_.state_.focused_window(), input_state_,
                                  clients);
      (void)router.route_input_grabbed(
          server_.state_.grabs(),
          gw::protocol::x11::CoreEventType::MotionNotify, 0, event.time_ms,
          new_target, event.state_before,
          glasswyrm::input::motion_delivery_mask(input_state_), event.root_x,
          event.root_y, new_target, clients);
      continue;
    }
    auto staged = input_state_;
    (void)staged.accept_wrapping_time(event.time_ms);
    if (event.kind == RealInputEventKind::Button) {
      if (staged.transition_button(event.detail, event.pressed) !=
          glasswyrm::input::TransitionStatus::Accepted)
        continue;
      bool consumed = false;
      if (interactive_policy_ &&
          interactive_policy_->kind() !=
              glasswyrm::wm::InteractionKind::None) {
        consumed = true;
        if (!event.pressed && interactive_policy_->release(
                                  event.detail, {event.root_x, event.root_y})) {
          (void)update_interactive_geometry();
          if (interactive_policy_->finish_ready())
            (void)interactive_policy_->finish();
        }
      } else if (event.pressed) {
        consumed = begin_interactive_pointer(event);
      }
      if (!consumed && event.pressed && !server_.state_.grabs().pointer_grab()) {
        if (!server_.state_.grabs().activate_passive_button(
                event.detail, static_cast<std::uint16_t>(event.state_before & 0xffU),
                event.time_ms)) {
          const auto recipient = router.input_recipient(
              input_state_.pointer_target(),
              gw::protocol::x11::event_mask::ButtonPress);
          if (recipient)
            (void)server_.state_.grabs().begin_automatic_button_grab(
                recipient->first, recipient->second, event.detail,
                event.time_ms);
        }
      }
      if (!consumed)
        (void)router.route_input_grabbed(
            server_.state_.grabs(),
            event.pressed ? gw::protocol::x11::CoreEventType::ButtonPress
                          : gw::protocol::x11::CoreEventType::ButtonRelease,
            event.detail, event.time_ms, input_state_.pointer_target(),
            event.state_before,
            event.pressed ? gw::protocol::x11::event_mask::ButtonPress
                          : gw::protocol::x11::event_mask::ButtonRelease,
            event.root_x, event.root_y, input_state_.pointer_target(), clients);
      input_state_ = staged;
      if (!consumed) {
        if (event.pressed)
          server_.state_.grabs().note_button_press(event.detail);
        else
          (void)server_.state_.grabs().note_button_release(event.detail);
      }
      if (consumed)
        continue;
      const auto target = input_state_.pointer_target();
      const auto *window = server_.state_.resources().find_window(target);
      const bool requests_focus =
          event.pressed && event.detail == 1 && window &&
          server_.state_.resources().is_policy_candidate(target) &&
          window->map_state == MapState::Viewable && window->policy_visible &&
          !window->cleanup_pending && !window->attributes.override_redirect &&
          server_.state_.focused_window() != target;
      if (!requests_focus)
        continue;
      const auto serial = server_.state_.next_lifecycle_serial();
      auto proposed = lifecycle_->committed();
      auto found = proposed.windows.find(target);
      if (!serial || found == proposed.windows.end())
        continue;
      found->second.focus_serial = *serial;
      LifecycleOperation operation;
      operation.token = next_lifecycle_token_++;
      operation.kind = LifecycleOperationKind::Focus;
      operation.window = target;
      operation.proposed = std::move(proposed);
      pending_real_focus_ = PendingRealFocus{server_.state_.focused_window()};
      const auto status = content_presenter_ && !bridge_->transaction_idle()
                              ? lifecycle_->enqueue_paused(std::move(operation))
                              : lifecycle_->enqueue(std::move(operation));
      if (status != EnqueueStatus::Queued)
        pending_real_focus_.reset();
      continue;
    }
    if (staged.transition_key(event.detail, event.pressed) !=
        glasswyrm::input::TransitionStatus::Accepted)
      continue;
    staged.set_core_modifier_mask(event.state_after);
    const auto focus =
        server_.state_.resources().find_window(event.focus_window)
            ? event.focus_window
            : server_.state_.screen().root_window;
    if (!handle_interactive_close(event))
      (void)router.route_input_grabbed(
          server_.state_.grabs(),
          event.pressed ? gw::protocol::x11::CoreEventType::KeyPress
                        : gw::protocol::x11::CoreEventType::KeyRelease,
          event.detail, event.time_ms, focus, event.state_before,
          event.pressed ? gw::protocol::x11::event_mask::KeyPress
                        : gw::protocol::x11::event_mask::KeyRelease,
          event.root_x, event.root_y, input_state_.pointer_target(), clients);
    input_state_ = staged;
  }
}

} // namespace glasswyrm::server
