#pragma once

#include "glasswyrmd/compositor_peer.hpp"
#include "output/model/types.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace glasswyrm::server {

struct SurfaceOutputProjection {
  std::uint64_t primary_output_id{};
  std::vector<std::uint64_t> output_ids;
  std::uint32_t preferred_scale_numerator{1};
  std::uint32_t preferred_scale_denominator{1};
  std::uint32_t client_buffer_scale{1};
  gwipc_surface_scale_mode scale_mode{GWIPC_SURFACE_SCALE_LEGACY};
  std::uint64_t layout_generation{};
};

[[nodiscard]] std::optional<SurfaceOutputProjection> project_surface_outputs(
    const output::OutputLayout &layout, std::uint64_t assigned_output_id,
    std::int32_t logical_x, std::int32_t logical_y, std::uint32_t logical_width,
    std::uint32_t logical_height, bool visible,
    std::uint32_t client_buffer_scale, gwipc_surface_scale_mode scale_mode);

[[nodiscard]] bool
populate_output_scene_records(CompositorSnapshotSubmission &submission,
                              const output::OutputLayout &layout);

[[nodiscard]] std::uint32_t
cursor_buffer_scale(const output::OutputLayout &layout, std::int32_t pointer_x,
                    std::int32_t pointer_y) noexcept;

[[nodiscard]] bool
populate_cursor_output_state(CompositorCursorSubmission &submission,
                             const output::OutputLayout &layout);

} // namespace glasswyrm::server
