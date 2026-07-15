#include "glasswyrmd/real_input_controller.hpp"

#include "protocol/x11/event_mask.hpp"

#include <algorithm>
#include <limits>

namespace glasswyrm::server {
namespace {

std::uint16_t button_state_bit(const std::uint8_t button) noexcept {
  namespace state = gw::protocol::x11::state_mask;
  switch (button) {
  case 1:
    return state::Button1;
  case 2:
    return state::Button2;
  case 3:
    return state::Button3;
  case 4:
    return state::Button4;
  case 5:
    return state::Button5;
  default:
    return 0;
  }
}

} // namespace

RealInputController::RealInputController(
    std::unique_ptr<glasswyrm::input::LibinputApi> api,
    std::unique_ptr<glasswyrm::input::XkbKeymap> keymap,
    std::unique_ptr<glasswyrm::input::RepeatState> repeat,
    std::unique_ptr<glasswyrm::input::RepeatTimer> timer)
    : api_(std::move(api)), backend_(*api_), keymap_(std::move(keymap)),
      repeat_(std::move(repeat)), repeat_timer_(std::move(timer)) {}

std::unique_ptr<RealInputController>
RealInputController::create(std::unique_ptr<glasswyrm::input::LibinputApi> api,
                            RealInputControllerConfig config,
                            std::string &error) {
  if (!api) {
    error = "real input API is unavailable";
    return nullptr;
  }
  auto keymap =
      glasswyrm::input::XkbKeymap::create(std::move(config.keymap), error);
  if (!keymap)
    return nullptr;
  auto repeat = glasswyrm::input::RepeatState::create(config.repeat, error);
  if (!repeat)
    return nullptr;
  auto timer = glasswyrm::input::RepeatTimer::create(error);
  if (!timer)
    return nullptr;
  auto result = std::unique_ptr<RealInputController>(new RealInputController(
      std::move(api), std::move(keymap), std::move(repeat), std::move(timer)));
  if (!result->backend_.initialize(config.device_paths, config.root_width,
                                   config.root_height, error))
    return nullptr;
  error.clear();
  return result;
}

RealInputServiceResult
RealInputController::service_backend(const std::uint32_t focus_window) {
  const auto serviced = backend_.service();
  backend_work_pending_ =
      serviced.status == glasswyrm::input::InputServiceStatus::BudgetExhausted;
  if (serviced.status == glasswyrm::input::InputServiceStatus::Fatal)
    return {false, false, serviced.error};
  if (serviced.status == glasswyrm::input::InputServiceStatus::Inactive)
    return {};
  auto result = convert(serviced.records, focus_window);
  const bool available = backend_.readiness().ready();
  if (serviced.provider_state_reset) {
    std::string reset_error;
    reset_provider_state(true, false, reset_error);
    if (!reset_error.empty())
      return {false, !available, std::move(reset_error)};
  }
  availability_reported_ = available;
  result.input_unavailable = !available;
  return result;
}

RealInputServiceResult RealInputController::convert(
    const std::span<const glasswyrm::input::RealInputRecord> records,
    const std::uint32_t focus_window) {
  for (const auto &record : records) {
    if (events_.size() >= kMaximumQueuedEvents)
      return {false, false, "real input event queue limit exceeded"};
    last_time_ms_ = record.time_ms;
    if (record.kind == glasswyrm::input::RealInputKind::MotionAbsolute ||
        record.kind == glasswyrm::input::RealInputKind::MotionRelative) {
      const auto state = state_mask();
      events_.push_back({RealInputEventKind::Motion, record.time_ms,
                         record.root_x, record.root_y, 0, 0, 0, false, state,
                         state});
      continue;
    }
    if (record.kind == glasswyrm::input::RealInputKind::Button) {
      const auto state_before = state_mask();
      const auto bit = button_state_bit(static_cast<std::uint8_t>(record.code));
      if (record.pressed)
        button_mask_ |= bit;
      else
        button_mask_ &= static_cast<std::uint16_t>(~bit);
      events_.push_back({RealInputEventKind::Button, record.time_ms,
                         record.root_x, record.root_y, 0,
                         static_cast<std::uint8_t>(record.code), 0, record.pressed,
                         state_before, state_mask()});
      continue;
    }
    if (record.kind != glasswyrm::input::RealInputKind::Key)
      continue;
    glasswyrm::input::PreparedKeyTransition transition;
    std::string transition_error;
    if (!keymap_->prepare_transition(record.code, record.pressed, transition,
                                     transition_error))
      continue;
    const auto state_before =
        static_cast<std::uint16_t>(button_mask_ | transition.core_state_before);
    if (!keymap_->apply_transition(transition, transition_error))
      return {false, false, std::move(transition_error)};
    const auto action =
        record.pressed ? repeat_->press(transition.x11_keycode, focus_window,
                                        transition.repeatable)
                       : repeat_->release(transition.x11_keycode);
    if (!apply_repeat_action(action, transition_error))
      return {false, false, std::move(transition_error)};
    events_.push_back({RealInputEventKind::Key, record.time_ms, record.root_x,
                       record.root_y, focus_window, transition.x11_keycode,
                       transition.keysym_before, record.pressed, state_before,
                       state_mask()});
  }
  return {};
}

RealInputServiceResult RealInputController::service_repeat() {
  std::string error;
  const auto read = repeat_timer_->read_expirations(error);
  if (read.status == glasswyrm::input::RepeatTimerReadStatus::Error)
    return {false, false, std::move(error)};
  if (read.status == glasswyrm::input::RepeatTimerReadStatus::WouldBlock)
    return {};
  const auto batch = repeat_->dispatch(read.expirations);
  std::uint32_t event_time = last_time_ms_;
  for (std::size_t index = 0; index < batch.events.size(); ++index) {
    if (events_.size() >= kMaximumQueuedEvents)
      return {false, false, "real input event queue limit exceeded"};
    if ((index % 2U) == 0)
      event_time = next_repeat_time();
    const auto &repeat_event = batch.events[index];
    const auto state = state_mask();
    events_.push_back(
        {RealInputEventKind::Key, event_time, backend_.pointer_x(),
         backend_.pointer_y(), repeat_event.key.focus, repeat_event.key.keycode,
         keymap_->core_keysyms(repeat_event.key.keycode)[0],
         repeat_event.kind == glasswyrm::input::RepeatEventKind::KeyPress,
         state, state});
  }
  return {};
}

RealInputSessionResult
RealInputController::apply_session_state(const gwipc_session_state state) {
  std::string error;
  if (state == GWIPC_SESSION_INACTIVE) {
    if (!backend_.active())
      return {GWIPC_SESSION_STATE_ALREADY_APPLIED, false, false, {}};
    reset_provider_state(false, true, error);
    if (!error.empty() || !backend_.suspend(error))
      return {GWIPC_SESSION_STATE_FAILED, false, true, std::move(error)};
    availability_reported_ = false;
    return {GWIPC_SESSION_STATE_ACCEPTED, false, true, {}};
  }
  if (state != GWIPC_SESSION_ACTIVE)
    return {GWIPC_SESSION_STATE_FAILED, false, false,
            "unsupported session state"};
  if (backend_.active()) {
    return {backend_.readiness().ready()
                ? GWIPC_SESSION_STATE_ALREADY_APPLIED
                : GWIPC_SESSION_STATE_INPUT_UNAVAILABLE,
            false,
            false,
            {}};
  }
  if (!backend_.resume(error))
    return {GWIPC_SESSION_STATE_FAILED, true, true, std::move(error)};
  availability_reported_ = true;
  return {GWIPC_SESSION_STATE_ACCEPTED, false, true, {}};
}

std::optional<RealInputEvent> RealInputController::take_event() {
  if (events_.empty())
    return std::nullopt;
  auto result = events_.front();
  events_.pop_front();
  return result;
}

void RealInputController::focus_changed(
    const std::uint32_t focus_window) noexcept {
  repeat_->focus_changed(focus_window, true);
}

void RealInputController::client_cleanup() noexcept {
  std::string ignored;
  (void)apply_repeat_action(repeat_->cancel_on_client_cleanup(), ignored);
}

bool RealInputController::apply_repeat_action(
    const glasswyrm::input::RepeatTimerAction action, std::string &error) {
  if (action == glasswyrm::input::RepeatTimerAction::Arm)
    return repeat_timer_->arm(repeat_->config(), error);
  if (action == glasswyrm::input::RepeatTimerAction::Disarm)
    return repeat_timer_->disarm(error);
  error.clear();
  return true;
}

std::uint16_t RealInputController::state_mask() const noexcept {
  return static_cast<std::uint16_t>(button_mask_ |
                                    keymap_->core_modifier_state());
}

std::uint32_t RealInputController::next_repeat_time() noexcept {
  ++last_time_ms_;
  if (last_time_ms_ == 0)
    last_time_ms_ = 1;
  return last_time_ms_;
}

void RealInputController::reset_provider_state(const bool publish_reset,
                                               const bool clear_events,
                                               std::string &error) {
  if (clear_events)
    events_.clear();
  button_mask_ = 0;
  backend_work_pending_ = false;
  if (!apply_repeat_action(repeat_->cancel_on_suspend(), error))
    return;
  if (!keymap_->reset_on_suspend(error))
    return;
  if (publish_reset)
    events_.push_back({RealInputEventKind::StateReset});
}

} // namespace glasswyrm::server
