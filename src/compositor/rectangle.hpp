#pragma once

#include <cstdint>
#include <optional>

namespace gw::compositor {

struct Rectangle {
  std::int32_t x{};
  std::int32_t y{};
  std::uint32_t width{};
  std::uint32_t height{};

  [[nodiscard]] constexpr bool empty() const noexcept {
    return width == 0 || height == 0;
  }

  friend constexpr bool operator==(const Rectangle&, const Rectangle&) = default;
};

[[nodiscard]] bool has_valid_extents(const Rectangle& rectangle) noexcept;
[[nodiscard]] std::optional<Rectangle>
intersection(const Rectangle& left, const Rectangle& right) noexcept;
[[nodiscard]] std::optional<Rectangle>
translate(const Rectangle& rectangle, std::int32_t dx, std::int32_t dy) noexcept;
[[nodiscard]] bool overlaps_or_is_compatibly_adjacent(
    const Rectangle& left, const Rectangle& right) noexcept;
[[nodiscard]] std::optional<Rectangle>
bounding_union(const Rectangle& left, const Rectangle& right) noexcept;

} // namespace gw::compositor
