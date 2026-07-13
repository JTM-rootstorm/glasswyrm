#pragma once

#include <array>
#include <cstdint>

namespace glasswyrm::input {

enum class TransitionStatus { Accepted, InvalidTransition, InvalidValue };

class InputState {
 public:
  [[nodiscard]] std::int32_t pointer_x() const noexcept { return pointer_x_; }
  [[nodiscard]] std::int32_t pointer_y() const noexcept { return pointer_y_; }
  [[nodiscard]] std::uint32_t pointer_target() const noexcept { return pointer_target_; }
  [[nodiscard]] std::uint32_t time() const noexcept { return time_; }
  [[nodiscard]] std::uint16_t mask() const noexcept;
  [[nodiscard]] bool button_down(std::uint8_t button) const noexcept;
  [[nodiscard]] bool key_down(std::uint8_t keycode) const noexcept { return keys_[keycode]; }
  [[nodiscard]] bool any_button_down() const noexcept;
  void set_pointer(std::int32_t x, std::int32_t y, std::uint32_t target) noexcept;
  void set_pointer_target(std::uint32_t target) noexcept { pointer_target_ = target; }
  void advance_time() noexcept;
  [[nodiscard]] bool accept_time(std::uint32_t time) noexcept;
  [[nodiscard]] TransitionStatus transition_button(std::uint8_t button, bool pressed) noexcept;
  [[nodiscard]] TransitionStatus transition_key(std::uint8_t keycode, bool pressed) noexcept;
  void reset_provider_state() noexcept;

 private:
  std::int32_t pointer_x_{0};
  std::int32_t pointer_y_{0};
  std::uint32_t pointer_target_{1};
  std::uint32_t time_{1};
  std::array<bool, 5> buttons_{};
  std::array<bool, 256> keys_{};
};

}  // namespace glasswyrm::input
