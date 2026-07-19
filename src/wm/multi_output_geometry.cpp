#include "wm/multi_output_policy.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace glasswyrm::wm {

Rectangle initial_placement(const OutputContext& output,
                            const std::uint32_t width,
                            const std::uint32_t height,
                            const std::size_t cascade_slot) noexcept {
  Rectangle result = output.work;
  result.width = std::min(width, output.work.width);
  result.height = std::min(height, output.work.height);
  const auto x_span = output.work.width - result.width;
  const auto y_span = output.work.height - result.height;
  result.x += static_cast<std::int32_t>(
      x_span == 0 ? 0 : (cascade_slot * 32U) % (x_span + 1U));
  result.y += static_cast<std::int32_t>(
      y_span == 0 ? 0 : (cascade_slot * 32U) % (y_span + 1U));
  return result;
}

Rectangle fullscreen_geometry(const OutputContext& output) noexcept {
  return output.work;
}

Rectangle maximize_geometry(const OutputContext& output) noexcept {
  return output.work;
}

}  // namespace glasswyrm::wm
