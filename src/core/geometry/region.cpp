#include "core/geometry/region.hpp"

#include <algorithm>
#include <tuple>

namespace {
using glasswyrm::geometry::Rectangle;
bool touches(const Rectangle left, const Rectangle right) {
  const auto lx2=left.x+static_cast<std::int64_t>(left.width), ly2=left.y+static_cast<std::int64_t>(left.height);
  const auto rx2=right.x+static_cast<std::int64_t>(right.width), ry2=right.y+static_cast<std::int64_t>(right.height);
  return left.x <= rx2 && right.x <= lx2 && left.y <= ry2 && right.y <= ly2;
}
Rectangle bounds(const Rectangle left, const Rectangle right) {
  const auto x1=std::min(left.x,right.x), y1=std::min(left.y,right.y);
  const auto x2=std::max(left.x+static_cast<std::int64_t>(left.width),right.x+static_cast<std::int64_t>(right.width));
  const auto y2=std::max(left.y+static_cast<std::int64_t>(left.height),right.y+static_cast<std::int64_t>(right.height));
  return {x1,y1,static_cast<std::uint32_t>(x2-x1),static_cast<std::uint32_t>(y2-y1)};
}
}

namespace glasswyrm::geometry {
void Region::add(const Rectangle rectangle) {
  const auto clipped = intersect(bounds_, rectangle);
  if (!clipped) return;
  rectangles_.push_back(*clipped);
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t first = 0; first < rectangles_.size() && !changed; ++first)
      for (std::size_t second = first + 1; second < rectangles_.size(); ++second)
        if (touches(rectangles_[first], rectangles_[second])) {
          rectangles_[first]=bounds(rectangles_[first],rectangles_[second]);
          rectangles_.erase(rectangles_.begin() + static_cast<std::ptrdiff_t>(second));
          changed = true; break;
        }
  }
  std::sort(rectangles_.begin(), rectangles_.end(), [](const auto& left, const auto& right) {
    return std::tie(left.y, left.x, left.height, left.width) <
           std::tie(right.y, right.x, right.height, right.width);
  });
  if (rectangles_.size() > kMaximumRectangles) rectangles_.assign(1, bounds_);
}
}  // namespace glasswyrm::geometry
