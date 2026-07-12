#include "core/geometry/region.hpp"

#include <algorithm>
#include <tuple>

namespace {
using glasswyrm::geometry::Rectangle;
std::vector<Rectangle> subtract(const Rectangle rectangle, const Rectangle cutter) {
  const auto overlap = glasswyrm::geometry::intersect(rectangle, cutter);
  if (!overlap) return {rectangle};
  std::vector<Rectangle> pieces;
  const auto right = rectangle.x + static_cast<std::int64_t>(rectangle.width);
  const auto bottom = rectangle.y + static_cast<std::int64_t>(rectangle.height);
  const auto overlap_right = overlap->x + static_cast<std::int64_t>(overlap->width);
  const auto overlap_bottom = overlap->y + static_cast<std::int64_t>(overlap->height);
  if (overlap->y > rectangle.y)
    pieces.push_back({rectangle.x, rectangle.y, rectangle.width,
                      static_cast<std::uint32_t>(overlap->y - rectangle.y)});
  if (overlap_bottom < bottom)
    pieces.push_back({rectangle.x, static_cast<std::int32_t>(overlap_bottom), rectangle.width,
                      static_cast<std::uint32_t>(bottom - overlap_bottom)});
  if (overlap->x > rectangle.x)
    pieces.push_back({rectangle.x, overlap->y,
                      static_cast<std::uint32_t>(overlap->x - rectangle.x), overlap->height});
  if (overlap_right < right)
    pieces.push_back({static_cast<std::int32_t>(overlap_right), overlap->y,
                      static_cast<std::uint32_t>(right - overlap_right), overlap->height});
  return pieces;
}
bool merge(Rectangle& left, const Rectangle right) {
  if (left.y == right.y && left.height == right.height &&
      left.x + static_cast<std::int64_t>(left.width) == right.x) {
    left.width += right.width; return true;
  }
  if (left.x == right.x && left.width == right.width &&
      left.y + static_cast<std::int64_t>(left.height) == right.y) {
    left.height += right.height; return true;
  }
  return false;
}
}

namespace glasswyrm::geometry {
void Region::add(const Rectangle rectangle) {
  const auto clipped = intersect(bounds_, rectangle);
  if (!clipped) return;
  std::vector<Rectangle> pending{*clipped};
  for (const auto existing : rectangles_) {
    std::vector<Rectangle> remaining;
    for (const auto piece : pending) {
      auto values = subtract(piece, existing);
      remaining.insert(remaining.end(), values.begin(), values.end());
    }
    pending = std::move(remaining);
  }
  rectangles_.insert(rectangles_.end(), pending.begin(), pending.end());
  bool changed = true;
  while (changed) {
    changed = false;
    std::sort(rectangles_.begin(), rectangles_.end(), [](const auto& left, const auto& right) {
      return std::tie(left.y, left.x, left.height, left.width) <
             std::tie(right.y, right.x, right.height, right.width);
    });
    for (std::size_t first = 0; first < rectangles_.size() && !changed; ++first)
      for (std::size_t second = first + 1; second < rectangles_.size(); ++second)
        if (merge(rectangles_[first], rectangles_[second])) {
          rectangles_.erase(rectangles_.begin() + static_cast<std::ptrdiff_t>(second));
          changed = true; break;
        }
  }
  if (rectangles_.size() > kMaximumRectangles) rectangles_.assign(1, bounds_);
}
}  // namespace glasswyrm::geometry
