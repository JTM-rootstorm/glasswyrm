#include "glasswyrmd/output_scene_projection.hpp"

#include <algorithm>
#include <limits>
#include <new>

namespace glasswyrm::server {
namespace {

bool intersects(const std::int32_t left_x, const std::int32_t left_y,
                const std::uint32_t left_width, const std::uint32_t left_height,
                const output::OutputState &right) noexcept {
  const auto left_end_x = static_cast<std::int64_t>(left_x) + left_width;
  const auto left_end_y = static_cast<std::int64_t>(left_y) + left_height;
  const auto right_end_x =
      static_cast<std::int64_t>(right.logical_x) + right.logical_width;
  const auto right_end_y =
      static_cast<std::int64_t>(right.logical_y) + right.logical_height;
  return left_x < right_end_x && left_y < right_end_y &&
         left_end_x > right.logical_x && left_end_y > right.logical_y;
}

bool contains(const output::OutputState &state, const std::int32_t x,
              const std::int32_t y) noexcept {
  return state.enabled && x >= state.logical_x && y >= state.logical_y &&
         static_cast<std::int64_t>(x) <
             static_cast<std::int64_t>(state.logical_x) + state.logical_width &&
         static_cast<std::int64_t>(y) <
             static_cast<std::int64_t>(state.logical_y) + state.logical_height;
}

output::OutputId cursor_primary(const output::OutputLayout &layout,
                                const std::int32_t pointer_x,
                                const std::int32_t pointer_y) noexcept {
  for (const auto id : layout.output_order)
    if (contains(layout.states.at(id), pointer_x, pointer_y))
      return id;
  return layout.primary_output_id;
}

gwipc_sdr_color_metadata color_record(const output::SdrMetadata &color) {
  return {static_cast<gwipc_sdr_color_space>(color.color_space),
          static_cast<gwipc_transfer_function>(color.transfer_function),
          static_cast<gwipc_color_primaries>(color.primaries),
          static_cast<std::uint8_t>(color.luminance_available),
          color.minimum_luminance_millinit,
          color.maximum_luminance_millinit,
          color.max_frame_average_luminance_millinit};
}

gwipc_output_upsert output_record(const output::OutputState &state) {
  gwipc_output_upsert record{};
  record.struct_size = sizeof(record);
  record.output_id = state.output_id.value;
  record.enabled = state.enabled;
  record.logical_x = state.logical_x;
  record.logical_y = state.logical_y;
  record.logical_width = state.logical_width;
  record.logical_height = state.logical_height;
  record.physical_pixel_width = state.physical_width;
  record.physical_pixel_height = state.physical_height;
  record.refresh_millihertz = state.refresh_millihertz;
  record.scale_numerator = state.scale.numerator;
  record.scale_denominator = state.scale.denominator;
  record.transform = static_cast<gwipc_transform>(state.transform);
  record.color = color_record(state.color);
  return record;
}

} // namespace

std::optional<SurfaceOutputProjection> project_surface_outputs(
    const output::OutputLayout &layout, const std::uint64_t assigned_output_id,
    const std::int32_t logical_x, const std::int32_t logical_y,
    const std::uint32_t logical_width, const std::uint32_t logical_height,
    const bool visible, const std::uint32_t client_buffer_scale,
    const gwipc_surface_scale_mode scale_mode) {
  try {
    const auto primary =
        layout.states.find(output::OutputId{assigned_output_id});
    if (primary == layout.states.end() || !primary->second.enabled ||
        logical_width == 0 || logical_height == 0 || client_buffer_scale == 0 ||
        client_buffer_scale > 4)
      return std::nullopt;
    SurfaceOutputProjection projected;
    projected.primary_output_id = assigned_output_id;
    projected.preferred_scale_numerator = primary->second.scale.numerator;
    projected.preferred_scale_denominator = primary->second.scale.denominator;
    projected.client_buffer_scale = client_buffer_scale;
    projected.scale_mode = scale_mode;
    projected.layout_generation = layout.generation;
    if (visible)
      for (const auto id : layout.output_order) {
        const auto &state = layout.states.at(id);
        if (state.enabled && intersects(logical_x, logical_y, logical_width,
                                        logical_height, state))
          projected.output_ids.push_back(id.value);
      }
    return projected;
  } catch (const std::bad_alloc &) {
    return std::nullopt;
  }
}

bool populate_output_scene_records(CompositorSnapshotSubmission &submission,
                                   const output::OutputLayout &layout) {
  try {
    submission.outputs.clear();
    submission.outputs.reserve(layout.output_order.size());
    submission.primary_output_id = layout.primary_output_id.value;
    submission.output_layout_generation = layout.generation;
    submission.outputs.push_back(
        output_record(layout.states.at(layout.primary_output_id)));
    for (const auto id : layout.output_order)
      if (id != layout.primary_output_id)
        submission.outputs.push_back(output_record(layout.states.at(id)));
    return submission.outputs.size() == layout.states.size();
  } catch (const std::bad_alloc &) {
    submission.outputs.clear();
    return false;
  }
}

std::uint32_t cursor_buffer_scale(const output::OutputLayout &layout,
                                  const std::int32_t pointer_x,
                                  const std::int32_t pointer_y) noexcept {
  const auto primary = cursor_primary(layout, pointer_x, pointer_y);
  const auto found = layout.states.find(primary);
  if (found == layout.states.end() || !found->second.enabled ||
      found->second.scale.denominator == 0)
    return 1;
  const auto scale =
      (found->second.scale.numerator + found->second.scale.denominator - 1U) /
      found->second.scale.denominator;
  return std::clamp(scale, UINT32_C(1), UINT32_C(4));
}

bool populate_cursor_output_state(CompositorCursorSubmission &submission,
                                  const output::OutputLayout &layout) {
  try {
    const auto primary =
        cursor_primary(layout, submission.pointer_x, submission.pointer_y);
    submission.surface.output_id = primary.value;
    const auto mode = submission.surface.scale_numerator == 1
                          ? GWIPC_SURFACE_SCALE_LEGACY
                          : GWIPC_SURFACE_SCALE_SCALED_PIXMAP;
    const auto projected = project_surface_outputs(
        layout, primary.value, submission.surface.logical_x,
        submission.surface.logical_y, submission.surface.logical_width,
        submission.surface.logical_height, submission.surface.visible != 0,
        submission.surface.scale_numerator, mode);
    if (!projected)
      return false;
    auto &membership = submission.surface_output;
    membership.output_ids = projected->output_ids;
    membership.state = {};
    membership.state.struct_size = sizeof(membership.state);
    membership.state.surface_id = submission.surface.surface_id;
    membership.state.primary_output_id = projected->primary_output_id;
    membership.state.output_count = membership.output_ids.size();
    membership.state.preferred_scale_numerator =
        projected->preferred_scale_numerator;
    membership.state.preferred_scale_denominator =
        projected->preferred_scale_denominator;
    membership.state.client_buffer_scale = projected->client_buffer_scale;
    membership.state.scale_mode = projected->scale_mode;
    membership.state.layout_generation = projected->layout_generation;
    return true;
  } catch (const std::bad_alloc &) {
    return false;
  }
}

} // namespace glasswyrm::server
