#include "wm/multi_output_policy.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace glasswyrm::wm {
namespace {

bool enabled(const std::map<std::uint64_t, OutputContext>& outputs,
             const std::uint64_t id) noexcept {
  const auto found = outputs.find(id);
  return found != outputs.end() && found->second.enabled;
}

std::uint64_t intersection_area(const Rectangle& left,
                                const Rectangle& right) noexcept {
  const auto left_right = static_cast<std::int64_t>(left.x) + left.width;
  const auto right_right = static_cast<std::int64_t>(right.x) + right.width;
  const auto left_bottom = static_cast<std::int64_t>(left.y) + left.height;
  const auto right_bottom = static_cast<std::int64_t>(right.y) + right.height;
  const auto width = std::max<std::int64_t>(
      0, std::min(left_right, right_right) - std::max(left.x, right.x));
  const auto height = std::max<std::int64_t>(
      0, std::min(left_bottom, right_bottom) - std::max(left.y, right.y));
  return static_cast<std::uint64_t>(width) *
         static_cast<std::uint64_t>(height);
}

bool intersects_any(const std::map<std::uint64_t, OutputContext>& outputs,
                    const Rectangle& geometry) noexcept {
  for (const auto& [id, output] : outputs) {
    (void)id;
    if (output.enabled && intersection_area(output.logical, geometry) != 0)
      return true;
  }
  return false;
}

std::int32_t visible_coordinate(const std::int32_t coordinate,
                                const std::int32_t output_origin,
                                const std::uint32_t output_extent,
                                const std::uint32_t window_extent) noexcept {
  const auto low = static_cast<std::int64_t>(output_origin) - window_extent + 1;
  const auto high = static_cast<std::int64_t>(output_origin) + output_extent - 1;
  return static_cast<std::int32_t>(
      std::clamp(static_cast<std::int64_t>(coordinate), low, high));
}

}  // namespace

std::uint64_t select_output(
    const std::map<std::uint64_t, OutputContext>& outputs,
    const std::uint64_t primary_output_id,
    const OutputSelection& selection) noexcept {
  if (enabled(outputs, selection.inherited_output_id))
    return selection.inherited_output_id;
  if (selection.retain_previous &&
      enabled(outputs, selection.previous_output_id))
    return selection.previous_output_id;

  std::uint64_t maximum_area = 0;
  std::vector<std::uint64_t> tied;
  for (const auto& [id, output] : outputs) {
    if (!output.enabled) continue;
    const auto area = intersection_area(selection.geometry, output.logical);
    if (area > maximum_area) {
      maximum_area = area;
      tied.clear();
    }
    if (area == maximum_area) tied.push_back(id);
  }
  const auto tied_contains = [&](const std::uint64_t id) {
    return id != 0 && std::ranges::find(tied, id) != tied.end();
  };
  if (tied_contains(selection.previous_output_id))
    return selection.previous_output_id;
  if (tied_contains(selection.preferred_output_id))
    return selection.preferred_output_id;
  if (tied_contains(primary_output_id)) return primary_output_id;
  return tied.empty() ? 0 : tied.front();
}

Rectangle retain_visible_pixel(
    const std::map<std::uint64_t, OutputContext>& outputs,
    const std::uint64_t assigned_output_id, Rectangle geometry) noexcept {
  if (intersects_any(outputs, geometry)) return geometry;
  const auto found = outputs.find(assigned_output_id);
  if (found == outputs.end() || !found->second.enabled) return geometry;
  const auto& logical = found->second.logical;
  geometry.x = visible_coordinate(geometry.x, logical.x, logical.width,
                                  geometry.width);
  geometry.y = visible_coordinate(geometry.y, logical.y, logical.height,
                                  geometry.height);
  return geometry;
}

}  // namespace glasswyrm::wm
