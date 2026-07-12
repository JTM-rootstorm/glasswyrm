#pragma once

#include "compositor/rectangle.hpp"

#include <cstddef>
#include <vector>

namespace gw::compositor {

class DamageRegion {
public:
  static constexpr std::size_t maximum_rectangles = 4096;

  explicit DamageRegion(Rectangle output_bounds);

  void add(Rectangle rectangle);
  void add_full_output();
  [[nodiscard]] const std::vector<Rectangle>& rectangles() const noexcept;
  [[nodiscard]] bool is_full_output() const noexcept;
  [[nodiscard]] bool empty() const noexcept;

private:
  void normalize();

  Rectangle output_bounds_;
  std::vector<Rectangle> rectangles_;
  bool full_output_{};
};

} // namespace gw::compositor
