#include "glasswyrmd/server_runtime.hpp"

#ifdef GW_SERVER_HAS_IPC

#include "input/input_router.hpp"
#include "protocol/x11/event_mask.hpp"

#include <cstdio>

namespace glasswyrm::server {

void ServerRuntime::service_input(const short listener_events,
                                  const short connection_events) {
  if ((listener_events & POLLIN) != 0) input_peer_->accept_provider();
  input_peer_->service(connection_events);
  if (input_peer_->consume_disconnect()) {
    if (pending_focus_input_)
      pending_focus_input_->provider_connected = false;
    input_state_.reset_provider_state();
    expected_input_id_ = 1;
    std::fprintf(stderr, "glasswyrmd: input provider state cleared\n");
  }
  const auto acknowledge = [&](const SyntheticInputRecord& record,
                               const gwipc_synthetic_input_result result,
                               const std::size_t delivered) {
    gwipc_synthetic_input_acknowledged value{};
    value.struct_size = sizeof(value);
    value.input_id = record.input_id;
    value.time_ms = input_state_.time();
    value.result = result;
    value.root_x = input_state_.pointer_x();
    value.root_y = input_state_.pointer_y();
    value.pointer_window = input_state_.pointer_target();
    value.focus_window = server_.state_.focused_window();
    value.state = input_state_.mask();
    value.delivered_event_count = static_cast<std::uint32_t>(delivered);
    if (!input_peer_->acknowledge(record, value)) input_peer_->disconnect();
  };
  std::size_t processed = 0;
  while (!pending_focus_input_ && processed++ < 64) {
    const auto next = input_peer_->take_record();
    if (!next) break;
    const auto record = *next;
    if (record.input_id != expected_input_id_) {
      input_peer_->disconnect();
      break;
    }
    ++expected_input_id_;
    std::vector<ClientConnection*> recipients;
    recipients.reserve(server_.clients_.size());
    for (const auto& client : server_.clients_)
      recipients.push_back(client.get());
    EventRouter router(server_.state_.resources());
    std::size_t delivered = 0;
    if (record.kind == SyntheticInputRecord::Kind::Barrier) {
      acknowledge(record, GWIPC_SYNTHETIC_INPUT_ACCEPTED, 0);
      continue;
    }
    if (record.time_ms == 0 || record.time_ms < input_state_.time()) {
      input_peer_->disconnect();
      break;
    }
    if (record.kind == SyntheticInputRecord::Kind::Motion) {
      const auto [x, y] = glasswyrm::input::clamp_pointer(
          record.root_x, record.root_y, server_.state_.screen().width_pixels,
          server_.state_.screen().height_pixels);
      const auto old_target = input_state_.pointer_target();
      const auto new_target = glasswyrm::input::hit_test_top_level(
          server_.state_.resources(), x, y);
      (void)input_state_.accept_time(record.time_ms);
      input_state_.set_pointer(x, y, new_target);
      delivered += router.route_crossing(old_target, new_target,
                                          server_.state_.focused_window(),
                                          input_state_, recipients);
      delivered += router.route_input(
          gw::protocol::x11::CoreEventType::MotionNotify, 0,
          input_state_.time(), new_target, input_state_.mask(),
          glasswyrm::input::motion_delivery_mask(input_state_), x, y,
          new_target, recipients);
      std::fprintf(stderr,
                   "glasswyrmd: input motion id=%llu root=%d,%d target=0x%08x delivered=%zu\n",
                   static_cast<unsigned long long>(record.input_id), x, y,
                   new_target, delivered);
      acknowledge(record,
                  x != record.root_x || y != record.root_y
                      ? GWIPC_SYNTHETIC_INPUT_CLAMPED
                      : GWIPC_SYNTHETIC_INPUT_ACCEPTED,
                  delivered);
      continue;
    }
    auto staged = input_state_;
    const auto pre_state = input_state_.mask();
    if (record.kind == SyntheticInputRecord::Kind::Button) {
      if (staged.transition_button(record.detail, record.pressed) !=
          glasswyrm::input::TransitionStatus::Accepted) {
        acknowledge(record, GWIPC_SYNTHETIC_INPUT_INVALID_TRANSITION, 0);
        continue;
      }
      (void)staged.accept_time(record.time_ms);
      delivered += router.route_input(
          record.pressed ? gw::protocol::x11::CoreEventType::ButtonPress
                         : gw::protocol::x11::CoreEventType::ButtonRelease,
          record.detail, record.time_ms, input_state_.pointer_target(),
          pre_state,
          record.pressed ? gw::protocol::x11::event_mask::ButtonPress
                         : gw::protocol::x11::event_mask::ButtonRelease,
          input_state_.pointer_x(), input_state_.pointer_y(),
          input_state_.pointer_target(), recipients);
      input_state_ = staged;
      const auto target = input_state_.pointer_target();
      const auto* window = server_.state_.resources().find_window(target);
      const bool requests_focus =
          record.pressed && record.detail == 1 && window &&
          server_.state_.resources().is_policy_candidate(target) &&
          window->map_state == MapState::Viewable && window->policy_visible &&
          !window->cleanup_pending && !window->attributes.override_redirect &&
          server_.state_.focused_window() != target;
      if (requests_focus) {
        const auto serial = server_.state_.next_lifecycle_serial();
        auto proposed = lifecycle_->committed();
        auto found = proposed.windows.find(target);
        if (!serial || found == proposed.windows.end()) {
          acknowledge(record, GWIPC_SYNTHETIC_INPUT_FOCUS_UNCHANGED,
                      delivered);
          continue;
        }
        found->second.focus_serial = *serial;
        LifecycleOperation operation;
        operation.token = next_lifecycle_token_++;
        operation.kind = LifecycleOperationKind::Focus;
        operation.window = target;
        operation.proposed = std::move(proposed);
        pending_focus_input_ = PendingFocusInput{
            record, server_.state_.focused_window(), delivered, true};
        const auto focus_status =
            content_presenter_ && !bridge_->transaction_idle()
                ? lifecycle_->enqueue_paused(std::move(operation))
                : lifecycle_->enqueue(std::move(operation));
        if (focus_status != EnqueueStatus::Queued) {
          pending_focus_input_.reset();
          acknowledge(record, GWIPC_SYNTHETIC_INPUT_FOCUS_REJECTED,
                      delivered);
        }
        continue;
      }
      acknowledge(record,
                  record.pressed && record.detail == 1
                      ? GWIPC_SYNTHETIC_INPUT_FOCUS_UNCHANGED
                      : GWIPC_SYNTHETIC_INPUT_ACCEPTED,
                  delivered);
      continue;
    }
    if (staged.transition_key(record.detail, record.pressed) !=
        glasswyrm::input::TransitionStatus::Accepted) {
      acknowledge(record, GWIPC_SYNTHETIC_INPUT_INVALID_TRANSITION, 0);
      continue;
    }
    (void)staged.accept_time(record.time_ms);
    const auto focus =
        server_.state_.resources().find_window(server_.state_.focused_window())
            ? server_.state_.focused_window()
            : server_.state_.screen().root_window;
    delivered += router.route_input(
        record.pressed ? gw::protocol::x11::CoreEventType::KeyPress
                       : gw::protocol::x11::CoreEventType::KeyRelease,
        record.detail, record.time_ms, focus, pre_state,
        record.pressed ? gw::protocol::x11::event_mask::KeyPress
                       : gw::protocol::x11::event_mask::KeyRelease,
        input_state_.pointer_x(), input_state_.pointer_y(),
        input_state_.pointer_target(), recipients);
    input_state_ = staged;
    acknowledge(record, GWIPC_SYNTHETIC_INPUT_ACCEPTED, delivered);
  }
}

}  // namespace glasswyrm::server

#endif
