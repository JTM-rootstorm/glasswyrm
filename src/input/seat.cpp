#include <glasswyrm/input/seat.hpp>

namespace glasswyrm::input {

Seat default_synthetic_seat() noexcept {
  return Seat{
      .name = "synthetic-seat0",
      .pointer_count = 1,
      .keyboard_count = 1,
  };
}

bool has_any_input_device(const Seat& seat) noexcept {
  return seat.pointer_count > 0 || seat.keyboard_count > 0;
}

}  // namespace glasswyrm::input
