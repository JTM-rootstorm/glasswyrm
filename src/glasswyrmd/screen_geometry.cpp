#include "glasswyrmd/screen_geometry.hpp"

#include "output/model/layout.hpp"

#include <cstdint>
#include <limits>

namespace glasswyrm::server {
namespace {

std::optional<std::uint16_t> millimeters_at_96_dpi(
    const std::uint32_t pixels) noexcept {
  constexpr std::uint64_t numerator = 254;
  constexpr std::uint64_t denominator = 960;
  const auto rounded =
      (static_cast<std::uint64_t>(pixels) * numerator + denominator / 2U) /
      denominator;
  if (rounded == 0 || rounded > std::numeric_limits<std::uint16_t>::max())
    return std::nullopt;
  return static_cast<std::uint16_t>(rounded);
}

}  // namespace

std::optional<gw::protocol::x11::ScreenModel> derive_output_screen_model(
    const output::OutputLayout& layout,
    gw::protocol::x11::ScreenModel fixed) {
  if (!output::validate_layout(layout) || layout.enabled_output_count == 0 ||
      layout.root_logical_width > std::numeric_limits<std::uint16_t>::max() ||
      layout.root_logical_height > std::numeric_limits<std::uint16_t>::max())
    return std::nullopt;
  const auto primary = layout.states.find(layout.primary_output_id);
  if (primary == layout.states.end() || !primary->second.enabled ||
      primary->second.refresh_millihertz == 0)
    return std::nullopt;
  const auto width_mm = millimeters_at_96_dpi(layout.root_logical_width);
  const auto height_mm = millimeters_at_96_dpi(layout.root_logical_height);
  if (!width_mm || !height_mm) return std::nullopt;

  fixed.width_pixels = static_cast<std::uint16_t>(layout.root_logical_width);
  fixed.height_pixels = static_cast<std::uint16_t>(layout.root_logical_height);
  fixed.width_millimeters = *width_mm;
  fixed.height_millimeters = *height_mm;
  fixed.refresh_millihertz = primary->second.refresh_millihertz;
  return fixed;
}

}  // namespace glasswyrm::server
