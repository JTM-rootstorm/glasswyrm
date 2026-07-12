#include "compositor/scene.hpp"

#include <algorithm>
#include <limits>
#include <set>

namespace gw::compositor {
namespace {

using OutputUpsert = gwipc_output_upsert;
using SdrColorMetadata = gwipc_sdr_color_metadata;
using SurfaceUpsert = gwipc_surface_upsert;

constexpr std::uint32_t kMaximumOutputExtent = 4096;
constexpr std::uint64_t kMaximumOutputPixels = 16'777'216;
constexpr std::size_t kMaximumSurfaces = 4096;

bool srgb(const SdrColorMetadata& color) {
  return color.color_space == GWIPC_SDR_COLOR_SPACE_SRGB &&
         color.transfer_function == GWIPC_TRANSFER_FUNCTION_SRGB &&
         color.primaries == GWIPC_COLOR_PRIMARIES_SRGB;
}

bool valid_output(const OutputUpsert& output) {
  if (output.output_id == 0 || output.logical_x != 0 || output.logical_y != 0 ||
      output.scale_numerator != 1 || output.scale_denominator != 1 ||
      output.transform != GWIPC_TRANSFORM_NORMAL || !srgb(output.color))
    return false;
  if (!output.enabled)
    return output.logical_width == 0 && output.logical_height == 0 &&
           output.physical_pixel_width == 0 && output.physical_pixel_height == 0;
  return output.logical_width != 0 && output.logical_height != 0 &&
         output.logical_width <= kMaximumOutputExtent &&
         output.logical_height <= kMaximumOutputExtent &&
         output.logical_width == output.physical_pixel_width &&
         output.logical_height == output.physical_pixel_height &&
         static_cast<std::uint64_t>(output.logical_width) * output.logical_height <=
             kMaximumOutputPixels;
}

bool checked_extent(std::int32_t origin, std::uint32_t extent) {
  if (extent == 0) return false;
  const auto end = static_cast<std::int64_t>(origin) + extent;
  return end <= std::numeric_limits<std::int32_t>::max() &&
         end >= std::numeric_limits<std::int32_t>::min();
}

bool valid_surface(const SurfaceUpsert& surface) {
  const bool metadata_only =
      surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_METADATA_ONLY;
  if (surface.surface_id == 0 ||
      (metadata_only ? surface.x11_window_id == 0 : surface.x11_window_id != 0) ||
      surface.parent_surface_id != 0 || surface.output_id == 0 ||
      surface.transform != GWIPC_TRANSFORM_NORMAL ||
      surface.scale_numerator != 1 || surface.scale_denominator != 1 ||
      !srgb(surface.color) ||
      (surface.presentation_flags != 0 && !metadata_only) ||
      surface.opacity > GWIPC_OPACITY_ONE ||
      !checked_extent(surface.logical_x, surface.logical_width) ||
      !checked_extent(surface.logical_y, surface.logical_height)) return false;
  if (!surface.clipping)
    return surface.clip_x == 0 && surface.clip_y == 0 &&
           surface.clip_width == 0 && surface.clip_height == 0;
  return checked_extent(surface.clip_x, surface.clip_width) &&
         checked_extent(surface.clip_y, surface.clip_height);
}

std::optional<Rectangle> effective_bounds(const SurfaceUpsert& surface,
                                          const OutputUpsert& output) {
  if (!surface.visible || !output.enabled || surface.opacity == 0 ||
      surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_METADATA_ONLY)
    return std::nullopt;
  Rectangle local{0, 0, surface.logical_width, surface.logical_height};
  if (surface.clipping) {
    const auto clipped = intersection(
        local, Rectangle{surface.clip_x, surface.clip_y, surface.clip_width,
                         surface.clip_height});
    if (!clipped) return std::nullopt;
    local = *clipped;
  }
  const auto placed = translate(local, surface.logical_x, surface.logical_y);
  if (!placed) return std::nullopt;
  return intersection(*placed, Rectangle{0, 0, output.logical_width,
                                         output.logical_height});
}

bool same_output_shape(const std::optional<OutputUpsert>& a,
                       const std::optional<OutputUpsert>& b) {
  if (!a || !b) return !a && !b;
  return a->enabled == b->enabled && a->logical_width == b->logical_width &&
         a->logical_height == b->logical_height;
}

} // namespace

bool SceneModel::mutations_allowed() const noexcept {
  return initial_snapshot_received_ || snapshot_active_;
}

bool SceneModel::begin_complete_snapshot() {
  if (snapshot_active_) return false;
  pre_snapshot_pending_ = pending_;
  pre_snapshot_damage_ = explicit_damage_;
  pending_ = {};
  explicit_damage_.clear();
  snapshot_active_ = true;
  return true;
}

bool SceneModel::end_complete_snapshot() {
  if (!snapshot_active_) return false;
  snapshot_active_ = false;
  initial_snapshot_received_ = true;
  pre_snapshot_pending_.reset();
  pre_snapshot_damage_.clear();
  return true;
}

void SceneModel::abort_complete_snapshot() {
  if (!snapshot_active_) return;
  pending_ = std::move(*pre_snapshot_pending_);
  explicit_damage_ = std::move(pre_snapshot_damage_);
  pre_snapshot_pending_.reset();
  snapshot_active_ = false;
}

bool SceneModel::apply(const OutputUpsert& output) {
  if (!mutations_allowed() || !valid_output(output)) return false;
  pending_.output = output;
  return true;
}

bool SceneModel::apply(const gwipc_output_remove& output) {
  if (!mutations_allowed() || output.output_id == 0 || !pending_.output ||
      pending_.output->output_id != output.output_id) return false;
  pending_.output.reset();
  return true;
}

bool SceneModel::apply(const SurfaceUpsert& surface) {
  if (!mutations_allowed() || !valid_surface(surface)) return false;
  if (!pending_.surfaces.contains(surface.surface_id) &&
      pending_.surfaces.size() == kMaximumSurfaces) return false;
  pending_.surfaces[surface.surface_id] = surface;
  return true;
}

bool SceneModel::apply(const gwipc_surface_policy_upsert& policy) {
  if (!mutations_allowed() || policy.surface_id == 0) return false;
  pending_.surface_policies[policy.surface_id] = policy;
  return true;
}

bool SceneModel::apply(const gwipc_surface_remove& surface) {
  if (!mutations_allowed() || surface.surface_id == 0 ||
      pending_.surfaces.erase(surface.surface_id) != 1)
    return false;
  pending_.surface_policies.erase(surface.surface_id);
  return true;
}

bool SceneModel::apply(const gwipc_surface_damage& damage) {
  if (!mutations_allowed() || !pending_.surfaces.contains(damage.surface_id) ||
      damage.rectangle_count > GWIPC_MAXIMUM_DAMAGE_RECTANGLES ||
      (damage.rectangle_count != 0 && !damage.rectangles)) return false;
  for (std::size_t index = 0; index < damage.rectangle_count; ++index) {
    const auto& rectangle = damage.rectangles[index];
    if (!checked_extent(rectangle.x, rectangle.width) ||
        !checked_extent(rectangle.y, rectangle.height)) return false;
  }
  explicit_damage_.push_back(
      {damage.surface_id,
       {damage.rectangles, damage.rectangles + damage.rectangle_count}});
  return true;
}

CommitResult SceneModel::commit(const gwipc_frame_commit& frame) {
  CommitResult result;
  result.presented_generation = presented_generation_;
  if (snapshot_active_ || !initial_snapshot_received_ || frame.commit_id == 0 ||
      frame.producer_generation == 0 || frame.flags != 0)
    return result;
  if (!pending_.output) {
    if (!pending_.surfaces.empty()) return result;
    committed_ = pending_;
    explicit_damage_.clear();
    presented_generation_ = frame.producer_generation;
    result.result = GWIPC_FRAME_DROPPED;
    result.presented_generation = presented_generation_;
    return result;
  }
  if (frame.output_id != 0 && pending_.output->output_id != frame.output_id) return result;
  for (const auto& [id, surface] : pending_.surfaces) {
    (void)id;
    if (surface.output_id != pending_.output->output_id) {
      result.result = GWIPC_FRAME_REJECTED_UNKNOWN_SURFACE;
      return result;
    }
  }
  for (const auto& [id, surface] : pending_.surfaces) {
    const bool metadata_only =
        surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_METADATA_ONLY;
    const auto policy = pending_.surface_policies.find(id);
    if (metadata_only) {
      if (policy == pending_.surface_policies.end() ||
          policy->second.x11_window_id != surface.x11_window_id) {
        result.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
        return result;
      }
    } else if (policy != pending_.surface_policies.end()) {
      result.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
      return result;
    }
  }
  for (const auto& [id, unused] : pending_.surface_policies) {
    (void)unused;
    if (!pending_.surfaces.contains(id)) {
      result.result = GWIPC_FRAME_REJECTED_UNKNOWN_SURFACE;
      return result;
    }
  }
  if (!pending_.output->enabled) {
    committed_ = pending_;
    pending_ = committed_;
    explicit_damage_.clear();
    presented_generation_ = frame.producer_generation;
    result.result = GWIPC_FRAME_DROPPED;
    result.presented_generation = presented_generation_;
    return result;
  }

  DamageRegion damage(Rectangle{0, 0, pending_.output->logical_width,
                                pending_.output->logical_height});
  if (!same_output_shape(committed_.output, pending_.output)) damage.add_full_output();
  std::set<std::uint64_t> ids;
  for (const auto& [id, unused] : committed_.surfaces) { (void)unused; ids.insert(id); }
  for (const auto& [id, unused] : pending_.surfaces) { (void)unused; ids.insert(id); }
  for (const auto id : ids) {
    const auto old = committed_.surfaces.find(id);
    const auto now = pending_.surfaces.find(id);
    const bool changed = old == committed_.surfaces.end() ||
                         now == pending_.surfaces.end() ||
                         old->second.logical_x != now->second.logical_x ||
                         old->second.logical_y != now->second.logical_y ||
                         old->second.logical_width != now->second.logical_width ||
                         old->second.logical_height != now->second.logical_height ||
                         old->second.stacking != now->second.stacking ||
                         old->second.visible != now->second.visible ||
                         old->second.clipping != now->second.clipping ||
                         old->second.clip_x != now->second.clip_x ||
                         old->second.clip_y != now->second.clip_y ||
                         old->second.clip_width != now->second.clip_width ||
                         old->second.clip_height != now->second.clip_height ||
                         old->second.opacity != now->second.opacity;
    if (!changed) continue;
    if (old != committed_.surfaces.end() && committed_.output)
      if (auto bounds = effective_bounds(old->second, *committed_.output)) damage.add(*bounds);
    if (now != pending_.surfaces.end())
      if (auto bounds = effective_bounds(now->second, *pending_.output)) damage.add(*bounds);
  }
  for (const auto& item : explicit_damage_) {
    const auto found = pending_.surfaces.find(item.surface_id);
    if (found == pending_.surfaces.end()) continue;
    for (const auto& rectangle : item.rectangles) {
      Rectangle local{rectangle.x, rectangle.y, rectangle.width, rectangle.height};
      auto clipped = intersection(local, Rectangle{0, 0, found->second.logical_width,
                                                   found->second.logical_height});
      if (clipped && found->second.clipping)
        clipped = intersection(*clipped, Rectangle{found->second.clip_x,
                                                   found->second.clip_y,
                                                   found->second.clip_width,
                                                   found->second.clip_height});
      if (!clipped) continue;
      const auto placed = translate(*clipped, found->second.logical_x,
                                    found->second.logical_y);
      if (placed) damage.add(*placed);
    }
  }

  committed_ = pending_;
  pending_ = committed_;
  explicit_damage_.clear();
  presented_generation_ = frame.producer_generation;
  result.result = GWIPC_FRAME_ACCEPTED;
  result.presented_generation = presented_generation_;
  result.damage = damage.rectangles();
  return result;
}

void SceneModel::disconnect() { *this = {}; }

std::vector<std::uint64_t> SceneModel::stacking_order() const {
  std::vector<const SurfaceUpsert*> surfaces;
  surfaces.reserve(committed_.surfaces.size());
  for (const auto& [id, surface] : committed_.surfaces) { (void)id; surfaces.push_back(&surface); }
  std::ranges::sort(surfaces, [](const auto* left, const auto* right) {
    return left->stacking < right->stacking ||
           (left->stacking == right->stacking && left->surface_id < right->surface_id);
  });
  std::vector<std::uint64_t> ids;
  ids.reserve(surfaces.size());
  for (const auto* surface : surfaces) ids.push_back(surface->surface_id);
  return ids;
}

} // namespace gw::compositor
