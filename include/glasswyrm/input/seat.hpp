#pragma once

#include <cstdint>
#include <string_view>

namespace glasswyrm::input {

struct Seat {
  std::string_view name;
  std::uint32_t pointer_count;
  std::uint32_t keyboard_count;
};

Seat default_synthetic_seat() noexcept;
bool has_any_input_device(const Seat& seat) noexcept;

}  // namespace glasswyrm::input
