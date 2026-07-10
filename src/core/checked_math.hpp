#pragma once

#include <cstddef>
#include <limits>
#include <optional>

namespace gw::core {

[[nodiscard]] constexpr std::optional<std::size_t>
checked_add(const std::size_t left, const std::size_t right) noexcept {
  if (right > std::numeric_limits<std::size_t>::max() - left) {
    return std::nullopt;
  }
  return left + right;
}

[[nodiscard]] constexpr std::optional<std::size_t>
checked_multiply(const std::size_t left, const std::size_t right) noexcept {
  if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
    return std::nullopt;
  }
  return left * right;
}

[[nodiscard]] constexpr std::optional<std::size_t>
checked_align_up(const std::size_t value,
                 const std::size_t alignment) noexcept {
  if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
    return std::nullopt;
  }
  const auto with_padding = checked_add(value, alignment - 1);
  if (!with_padding) {
    return std::nullopt;
  }
  return *with_padding & ~(alignment - 1);
}

} // namespace gw::core
