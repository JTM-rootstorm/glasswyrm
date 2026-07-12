#include "core/geometry/region.hpp"

namespace glasswyrm::geometry {
void Region::add(const Rectangle rectangle) {
  const auto clipped = intersect(bounds_, rectangle);
  if (!clipped) return;
  if (rectangles_.size() >= kMaximumRectangles) {
    rectangles_.assign(1, bounds_);
    return;
  }
  rectangles_.push_back(*clipped);
}
}  // namespace glasswyrm::geometry
