#include "compositor/scene_validation.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <set>
#include <tuple>

namespace gw::compositor {
namespace {

constexpr std::size_t kMaximumSurfaces = 4096;
constexpr std::uint32_t kMaximumCursorExtent = 64;

bool cursor_surface(const gwipc_surface_upsert &surface) noexcept {
  return surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_CURSOR;
}

bool metadata_surface(const gwipc_surface_upsert &surface) noexcept {
  return surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_METADATA_ONLY;
}

bool srgb(const gwipc_sdr_color_metadata &color) noexcept {
  return color.color_space == GWIPC_SDR_COLOR_SPACE_SRGB &&
         color.transfer_function == GWIPC_TRANSFER_FUNCTION_SRGB &&
         color.primaries == GWIPC_COLOR_PRIMARIES_SRGB;
}

bool checked_extent(const std::int32_t origin,
                    const std::uint32_t extent) noexcept {
  if (extent == 0)
    return false;
  const auto end = static_cast<std::int64_t>(origin) + extent;
  return end <= std::numeric_limits<std::int32_t>::max() &&
         end >= std::numeric_limits<std::int32_t>::min();
}

bool valid_transform(const gwipc_transform transform) noexcept {
  return transform >= GWIPC_TRANSFORM_NORMAL &&
         transform <= GWIPC_TRANSFORM_FLIPPED_270;
}

bool transform_swaps_extents(const gwipc_transform transform) noexcept {
  return transform == GWIPC_TRANSFORM_ROTATE_90 ||
         transform == GWIPC_TRANSFORM_ROTATE_270 ||
         transform == GWIPC_TRANSFORM_FLIPPED_90 ||
         transform == GWIPC_TRANSFORM_FLIPPED_270;
}

bool valid_scale(const std::uint32_t numerator,
                 const std::uint32_t denominator) noexcept {
  return denominator != 0 &&
         denominator <= GWIPC_MAXIMUM_OUTPUT_SCALE_DENOMINATOR &&
         std::gcd(numerator, denominator) == 1 && numerator >= denominator &&
         static_cast<std::uint64_t>(numerator) <=
             static_cast<std::uint64_t>(GWIPC_MAXIMUM_OUTPUT_SCALE_NUMERATOR) *
                 denominator;
}

std::uint32_t logical_dimension(const std::uint32_t physical,
                                const std::uint32_t numerator,
                                const std::uint32_t denominator) noexcept {
  const auto scaled = static_cast<std::uint64_t>(physical) * denominator;
  return static_cast<std::uint32_t>(scaled / numerator +
                                    (scaled % numerator != 0 ? 1U : 0U));
}

Rectangle output_rectangle(const gwipc_output_upsert &output) noexcept {
  return {output.logical_x, output.logical_y, output.logical_width,
          output.logical_height};
}

std::vector<std::uint64_t>
geometric_memberships(const Scene &scene, const gwipc_surface_upsert &surface) {
  std::vector<std::uint64_t> result;
  if (!surface.visible)
    return result;
  const Rectangle surface_rectangle{surface.logical_x, surface.logical_y,
                                    surface.logical_width,
                                    surface.logical_height};
  for (const auto &[id, output] : scene.outputs)
    if (output.enabled &&
        intersection(surface_rectangle, output_rectangle(output)))
      result.push_back(id);
  std::ranges::sort(result, [&scene](const auto left_id, const auto right_id) {
    const auto &left = scene.outputs.at(left_id);
    const auto &right = scene.outputs.at(right_id);
    return std::tie(left.logical_y, left.logical_x, left_id) <
           std::tie(right.logical_y, right.logical_x, right_id);
  });
  return result;
}

bool overlapping_outputs(const Scene &scene) noexcept {
  for (auto left = scene.outputs.begin(); left != scene.outputs.end(); ++left) {
    if (!left->second.enabled)
      continue;
    for (auto right = std::next(left); right != scene.outputs.end(); ++right)
      if (right->second.enabled &&
          intersection(output_rectangle(left->second),
                       output_rectangle(right->second)))
        return true;
  }
  return false;
}

} // namespace

bool valid_scene_output(const gwipc_output_upsert &output,
                        const SceneProfile profile) noexcept {
  if (output.output_id == 0 || !srgb(output.color))
    return false;
  if (profile == SceneProfile::Historical) {
    if (output.logical_x != 0 || output.logical_y != 0 ||
        output.scale_numerator != 1 || output.scale_denominator != 1 ||
        output.transform != GWIPC_TRANSFORM_NORMAL)
      return false;
    if (!output.enabled)
      return output.logical_width == 0 && output.logical_height == 0 &&
             output.physical_pixel_width == 0 &&
             output.physical_pixel_height == 0;
    return output.logical_width != 0 && output.logical_height != 0 &&
           output.logical_width <= GWIPC_MAXIMUM_OUTPUT_PHYSICAL_EXTENT &&
           output.logical_height <= GWIPC_MAXIMUM_OUTPUT_PHYSICAL_EXTENT &&
           output.logical_width == output.physical_pixel_width &&
           output.logical_height == output.physical_pixel_height &&
           static_cast<std::uint64_t>(output.logical_width) *
                   output.logical_height <=
               GWIPC_MAXIMUM_OUTPUT_PHYSICAL_PIXELS;
  }

  if (!valid_scale(output.scale_numerator, output.scale_denominator) ||
      !valid_transform(output.transform))
    return false;
  if (!output.enabled)
    return output.logical_x == 0 && output.logical_y == 0 &&
           output.logical_width == 0 && output.logical_height == 0 &&
           output.physical_pixel_width == 0 &&
           output.physical_pixel_height == 0 && output.refresh_millihertz == 0;
  if (output.logical_x < 0 || output.logical_y < 0 ||
      output.logical_width == 0 || output.logical_height == 0 ||
      output.physical_pixel_width == 0 || output.physical_pixel_height == 0 ||
      output.refresh_millihertz == 0 ||
      output.physical_pixel_width > GWIPC_MAXIMUM_OUTPUT_PHYSICAL_EXTENT ||
      output.physical_pixel_height > GWIPC_MAXIMUM_OUTPUT_PHYSICAL_EXTENT ||
      static_cast<std::uint64_t>(output.physical_pixel_width) *
              output.physical_pixel_height >
          GWIPC_MAXIMUM_OUTPUT_PHYSICAL_PIXELS ||
      !checked_extent(output.logical_x, output.logical_width) ||
      !checked_extent(output.logical_y, output.logical_height) ||
      static_cast<std::uint64_t>(output.logical_x) + output.logical_width >
          GWIPC_MAXIMUM_ROOT_LOGICAL_WIDTH ||
      static_cast<std::uint64_t>(output.logical_y) + output.logical_height >
          GWIPC_MAXIMUM_ROOT_LOGICAL_HEIGHT)
    return false;
  auto physical_width = output.physical_pixel_width;
  auto physical_height = output.physical_pixel_height;
  if (transform_swaps_extents(output.transform))
    std::swap(physical_width, physical_height);
  return output.logical_width == logical_dimension(physical_width,
                                                   output.scale_numerator,
                                                   output.scale_denominator) &&
         output.logical_height == logical_dimension(physical_height,
                                                    output.scale_numerator,
                                                    output.scale_denominator);
}

bool valid_scene_surface(const gwipc_surface_upsert &surface,
                         const SceneProfile profile) noexcept {
  const bool metadata_only = metadata_surface(surface);
  const bool cursor = cursor_surface(surface);
  const bool valid_client_scale =
      profile == SceneProfile::Historical
          ? surface.scale_numerator == 1 && surface.scale_denominator == 1
          : surface.scale_numerator >= 1 && surface.scale_numerator <= 4 &&
                surface.scale_denominator == 1;
  if (surface.surface_id == 0 ||
      (metadata_only && surface.x11_window_id == 0) ||
      surface.parent_surface_id != 0 || surface.output_id == 0 ||
      surface.transform != GWIPC_TRANSFORM_NORMAL || !valid_client_scale ||
      !srgb(surface.color) ||
      (surface.presentation_flags != 0 && !metadata_only && !cursor) ||
      surface.opacity > GWIPC_OPACITY_ONE ||
      !checked_extent(surface.logical_x, surface.logical_width) ||
      !checked_extent(surface.logical_y, surface.logical_height))
    return false;
  if (cursor && (surface.x11_window_id != 0 ||
                 surface.logical_width > kMaximumCursorExtent ||
                 surface.logical_height > kMaximumCursorExtent ||
                 surface.clipping || surface.opacity != GWIPC_OPACITY_ONE ||
                 surface.fullscreen_eligible != GWIPC_TRI_STATE_UNKNOWN ||
                 surface.direct_scanout_eligible != GWIPC_TRI_STATE_UNKNOWN))
    return false;
  if (!surface.clipping)
    return surface.clip_x == 0 && surface.clip_y == 0 &&
           surface.clip_width == 0 && surface.clip_height == 0;
  return checked_extent(surface.clip_x, surface.clip_width) &&
         checked_extent(surface.clip_y, surface.clip_height);
}

bool valid_surface_output_state(
    const gwipc_surface_output_state &state) noexcept {
  if (state.surface_id == 0 || state.primary_output_id == 0 ||
      state.output_count > GWIPC_MAXIMUM_OUTPUTS ||
      (state.output_count != 0 && state.output_ids == nullptr) ||
      state.preferred_scale_numerator == 0 ||
      state.preferred_scale_denominator == 0 ||
      state.client_buffer_scale == 0 || state.client_buffer_scale > 4 ||
      (state.scale_mode != GWIPC_SURFACE_SCALE_LEGACY &&
       state.scale_mode != GWIPC_SURFACE_SCALE_SCALED_PIXMAP) ||
      state.layout_generation == 0 || state.flags != 0)
    return false;
  if (state.scale_mode == GWIPC_SURFACE_SCALE_LEGACY &&
      state.client_buffer_scale != 1)
    return false;
  std::set<std::uint64_t> identifiers;
  for (std::size_t index = 0; index < state.output_count; ++index)
    if (state.output_ids[index] == 0 ||
        !identifiers.insert(state.output_ids[index]).second)
      return false;
  return true;
}

void infer_historical_output_state(Scene &scene) {
  scene.outputs.clear();
  scene.surface_outputs.clear();
  scene.primary_output_id = 0;
  scene.configuration_generation = 0;
  if (!scene.output)
    return;
  scene.outputs.emplace(scene.output->output_id, *scene.output);
  scene.primary_output_id = scene.output->output_id;
  for (const auto &[id, surface] : scene.surfaces) {
    if (metadata_surface(surface))
      continue;
    scene.surface_outputs.emplace(
        id, SurfaceOutputMembership{surface.output_id,
                                    {surface.output_id},
                                    1,
                                    1,
                                    1,
                                    GWIPC_SURFACE_SCALE_LEGACY,
                                    0,
                                    0});
  }
}

gwipc_frame_result validate_output_model_scene(const Scene &scene) noexcept {
  if (scene.output || scene.outputs.empty() ||
      scene.outputs.size() > GWIPC_MAXIMUM_OUTPUTS ||
      scene.primary_output_id == 0 || scene.configuration_generation == 0 ||
      overlapping_outputs(scene))
    return GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;

  std::size_t enabled_count = 0;
  std::uint64_t total_pixels = 0;
  for (const auto &[id, output] : scene.outputs) {
    if (id != output.output_id ||
        !valid_scene_output(output, SceneProfile::OutputModel))
      return GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    if (!output.enabled)
      continue;
    ++enabled_count;
    const auto pixels =
        static_cast<std::uint64_t>(output.physical_pixel_width) *
        output.physical_pixel_height;
    if (pixels > GWIPC_MAXIMUM_TOTAL_OUTPUT_PIXELS - total_pixels)
      return GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    total_pixels += pixels;
  }
  const auto primary = scene.outputs.find(scene.primary_output_id);
  if (enabled_count == 0 || primary == scene.outputs.end() ||
      !primary->second.enabled)
    return GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;

  if (scene.surfaces.size() > kMaximumSurfaces)
    return GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
  for (const auto &[id, surface] : scene.surfaces) {
    if (id != surface.surface_id ||
        !valid_scene_surface(surface, SceneProfile::OutputModel))
      return GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    const auto assigned = scene.outputs.find(surface.output_id);
    if (assigned == scene.outputs.end() || !assigned->second.enabled)
      return GWIPC_FRAME_REJECTED_UNKNOWN_SURFACE;
    const auto membership = scene.surface_outputs.find(id);
    if (metadata_surface(surface)) {
      if (membership != scene.surface_outputs.end())
        return GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
      continue;
    }
    if (membership == scene.surface_outputs.end())
      return GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    const auto &state = membership->second;
    if (state.primary_output_id != surface.output_id ||
        state.layout_generation != scene.configuration_generation ||
        state.preferred_scale_numerator != assigned->second.scale_numerator ||
        state.preferred_scale_denominator !=
            assigned->second.scale_denominator ||
        state.client_buffer_scale != surface.scale_numerator ||
        state.output_ids != geometric_memberships(scene, surface))
      return GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
    if (!state.output_ids.empty() &&
        std::ranges::find(state.output_ids, state.primary_output_id) ==
            state.output_ids.end())
      return GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
  }
  for (const auto &[id, unused] : scene.surface_outputs) {
    (void)unused;
    if (!scene.surfaces.contains(id))
      return GWIPC_FRAME_REJECTED_UNKNOWN_SURFACE;
  }
  return GWIPC_FRAME_ACCEPTED;
}

} // namespace gw::compositor
