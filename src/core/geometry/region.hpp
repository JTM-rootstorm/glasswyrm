#pragma once

#include "core/geometry/rectangle.hpp"

#include <cstddef>
#include <vector>

namespace glasswyrm::geometry {

class Region {
 public:
  static constexpr std::size_t kMaximumRectangles = 1024;
  explicit Region(Rectangle bounds) : bounds_(bounds) {}
  void add(Rectangle rectangle);
  [[nodiscard]] const std::vector<Rectangle>& rectangles() const noexcept { return rectangles_; }
  [[nodiscard]] bool empty() const noexcept { return rectangles_.empty(); }
 private:
  Rectangle bounds_;
  std::vector<Rectangle> rectangles_;
};

}  // namespace glasswyrm::geometry
