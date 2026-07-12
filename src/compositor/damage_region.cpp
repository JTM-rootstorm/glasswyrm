#include "compositor/damage_region.hpp"

#include <algorithm>

namespace gw::compositor {

DamageRegion::DamageRegion(const Rectangle output_bounds)
    : output_bounds_(output_bounds) {}

void DamageRegion::add(const Rectangle rectangle) {
  if (full_output_) {
    return;
  }
  const auto clipped = intersection(rectangle, output_bounds_);
  if (!clipped) {
    return;
  }
  Rectangle candidate = *clipped;
  for (std::size_t index = 0; index < rectangles_.size();) {
    if (!overlaps_or_is_compatibly_adjacent(candidate, rectangles_[index])) {
      ++index;
      continue;
    }
    const auto united = bounding_union(candidate, rectangles_[index]);
    if (!united) {
      add_full_output();
      return;
    }
    candidate = *united;
    rectangles_.erase(rectangles_.begin() + static_cast<std::ptrdiff_t>(index));
    // The enlarged candidate can now touch a rectangle already examined.
    index = 0;
  }
  rectangles_.push_back(candidate);
  if (rectangles_.size() > maximum_rectangles) {
    add_full_output();
    return;
  }
  normalize();
}

void DamageRegion::add_full_output() {
  rectangles_.clear();
  if (!output_bounds_.empty() && has_valid_extents(output_bounds_)) {
    rectangles_.push_back(output_bounds_);
  }
  full_output_ = true;
}

const std::vector<Rectangle>& DamageRegion::rectangles() const noexcept {
  return rectangles_;
}

bool DamageRegion::is_full_output() const noexcept { return full_output_; }

bool DamageRegion::empty() const noexcept { return rectangles_.empty(); }

void DamageRegion::normalize() {
  const auto order = [](const Rectangle& left, const Rectangle& right) {
    if (left.y != right.y) return left.y < right.y;
    if (left.x != right.x) return left.x < right.x;
    if (left.height != right.height) return left.height < right.height;
    return left.width < right.width;
  };

  std::sort(rectangles_.begin(), rectangles_.end(), order);
}

} // namespace gw::compositor
