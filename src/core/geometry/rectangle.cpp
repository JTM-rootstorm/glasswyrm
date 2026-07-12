#include "core/geometry/rectangle.hpp"

#include <algorithm>
#include <cstdint>

namespace glasswyrm::geometry {

std::optional<Rectangle> intersect(const Rectangle left,
                                   const Rectangle right) noexcept {
  const auto left_x2 = static_cast<std::int64_t>(left.x) + left.width;
  const auto left_y2 = static_cast<std::int64_t>(left.y) + left.height;
  const auto right_x2 = static_cast<std::int64_t>(right.x) + right.width;
  const auto right_y2 = static_cast<std::int64_t>(right.y) + right.height;
  const auto x1 = std::max<std::int64_t>(left.x, right.x);
  const auto y1 = std::max<std::int64_t>(left.y, right.y);
  const auto x2 = std::min(left_x2, right_x2);
  const auto y2 = std::min(left_y2, right_y2);
  if (x2 <= x1 || y2 <= y1) return std::nullopt;
  return Rectangle{static_cast<std::int32_t>(x1), static_cast<std::int32_t>(y1),
                   static_cast<std::uint32_t>(x2 - x1),
                   static_cast<std::uint32_t>(y2 - y1)};
}

}  // namespace glasswyrm::geometry
