#include "compositor/rectangle.hpp"

#include <algorithm>
#include <limits>

namespace gw::compositor {
namespace {

using Wide = std::int64_t;

[[nodiscard]] constexpr Wide right(const Rectangle& rectangle) noexcept {
  return static_cast<Wide>(rectangle.x) + rectangle.width;
}

[[nodiscard]] constexpr Wide bottom(const Rectangle& rectangle) noexcept {
  return static_cast<Wide>(rectangle.y) + rectangle.height;
}

[[nodiscard]] bool coordinate_fits(const Wide value) noexcept {
  return value >= std::numeric_limits<std::int32_t>::min() &&
         value <= std::numeric_limits<std::int32_t>::max();
}

} // namespace

bool has_valid_extents(const Rectangle& rectangle) noexcept {
  return coordinate_fits(right(rectangle)) && coordinate_fits(bottom(rectangle));
}

std::optional<Rectangle> intersection(const Rectangle& left,
                                      const Rectangle& right_rectangle) noexcept {
  if (left.empty() || right_rectangle.empty() || !has_valid_extents(left) ||
      !has_valid_extents(right_rectangle)) {
    return std::nullopt;
  }
  const Wide x1 = std::max<Wide>(left.x, right_rectangle.x);
  const Wide y1 = std::max<Wide>(left.y, right_rectangle.y);
  const Wide x2 = std::min(right(left), right(right_rectangle));
  const Wide y2 = std::min(bottom(left), bottom(right_rectangle));
  if (x1 >= x2 || y1 >= y2) {
    return std::nullopt;
  }
  return Rectangle{static_cast<std::int32_t>(x1), static_cast<std::int32_t>(y1),
                   static_cast<std::uint32_t>(x2 - x1),
                   static_cast<std::uint32_t>(y2 - y1)};
}

std::optional<Rectangle> translate(const Rectangle& rectangle,
                                   const std::int32_t dx,
                                   const std::int32_t dy) noexcept {
  if (!has_valid_extents(rectangle)) {
    return std::nullopt;
  }
  const Wide new_x = static_cast<Wide>(rectangle.x) + dx;
  const Wide new_y = static_cast<Wide>(rectangle.y) + dy;
  const Wide new_right = right(rectangle) + dx;
  const Wide new_bottom = bottom(rectangle) + dy;
  if (!coordinate_fits(new_x) || !coordinate_fits(new_y) ||
      !coordinate_fits(new_right) || !coordinate_fits(new_bottom)) {
    return std::nullopt;
  }
  return Rectangle{static_cast<std::int32_t>(new_x),
                   static_cast<std::int32_t>(new_y), rectangle.width,
                   rectangle.height};
}

bool overlaps_or_is_compatibly_adjacent(const Rectangle& left,
                                         const Rectangle& other) noexcept {
  if (left.empty() || other.empty() || !has_valid_extents(left) ||
      !has_valid_extents(other)) {
    return false;
  }
  const bool overlaps = static_cast<Wide>(left.x) < right(other) &&
                        static_cast<Wide>(other.x) < right(left) &&
                        static_cast<Wide>(left.y) < bottom(other) &&
                        static_cast<Wide>(other.y) < bottom(left);
  const bool horizontal = left.y == other.y && left.height == other.height &&
                          right(left) >= other.x && right(other) >= left.x;
  const bool vertical = left.x == other.x && left.width == other.width &&
                        bottom(left) >= other.y && bottom(other) >= left.y;
  return overlaps || horizontal || vertical;
}

std::optional<Rectangle> bounding_union(const Rectangle& left,
                                        const Rectangle& other) noexcept {
  if (!has_valid_extents(left) || !has_valid_extents(other)) {
    return std::nullopt;
  }
  if (left.empty()) {
    return other;
  }
  if (other.empty()) {
    return left;
  }
  const Wide x1 = std::min<Wide>(left.x, other.x);
  const Wide y1 = std::min<Wide>(left.y, other.y);
  const Wide x2 = std::max(right(left), right(other));
  const Wide y2 = std::max(bottom(left), bottom(other));
  if (!coordinate_fits(x1) || !coordinate_fits(y1) || !coordinate_fits(x2) ||
      !coordinate_fits(y2)) {
    return std::nullopt;
  }
  return Rectangle{static_cast<std::int32_t>(x1), static_cast<std::int32_t>(y1),
                   static_cast<std::uint32_t>(x2 - x1),
                   static_cast<std::uint32_t>(y2 - y1)};
}

} // namespace gw::compositor
