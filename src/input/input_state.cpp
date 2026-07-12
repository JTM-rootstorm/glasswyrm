#include "input/input_state.hpp"

#include "protocol/x11/event_mask.hpp"

#include <algorithm>
#include <limits>

namespace glasswyrm::input {
namespace sm = gw::protocol::x11::state_mask;

std::uint16_t InputState::mask() const noexcept {
  std::uint16_t result = 0;
  const std::array<std::uint16_t, 5> button_masks{
      sm::Button1, sm::Button2, sm::Button3, sm::Button4, sm::Button5};
  for (std::size_t i = 0; i < buttons_.size(); ++i)
    if (buttons_[i]) result |= button_masks[i];
  if (keys_[37] || keys_[105]) result |= sm::Control;
  if (keys_[50] || keys_[62]) result |= sm::Shift;
  if (keys_[64] || keys_[108]) result |= sm::Mod1;
  return result;
}

bool InputState::button_down(const std::uint8_t button) const noexcept {
  return button >= 1 && button <= 5 && buttons_[button - 1];
}

bool InputState::any_button_down() const noexcept {
  return std::ranges::any_of(buttons_, [](const bool down) { return down; });
}

void InputState::set_pointer(const std::int32_t x, const std::int32_t y,
                             const std::uint32_t target) noexcept {
  pointer_x_ = x; pointer_y_ = y; pointer_target_ = target;
}

void InputState::advance_time() noexcept {
  if (time_ != std::numeric_limits<std::uint32_t>::max()) ++time_;
}

bool InputState::accept_time(const std::uint32_t time) noexcept {
  if (time == 0 || time < time_) return false;
  time_ = time;
  return true;
}

TransitionStatus InputState::transition_button(const std::uint8_t button,
                                                const bool pressed) noexcept {
  if (button < 1 || button > 5) return TransitionStatus::InvalidValue;
  auto& state = buttons_[button - 1];
  if (state == pressed) return TransitionStatus::InvalidTransition;
  state = pressed;
  return TransitionStatus::Accepted;
}

TransitionStatus InputState::transition_key(const std::uint8_t keycode,
                                             const bool pressed) noexcept {
  auto& state = keys_[keycode];
  if (state == pressed) return TransitionStatus::InvalidTransition;
  state = pressed;
  return TransitionStatus::Accepted;
}

void InputState::reset_provider_state() noexcept {
  buttons_.fill(false);
  keys_.fill(false);
}

}  // namespace glasswyrm::input
