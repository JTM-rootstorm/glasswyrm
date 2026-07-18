#include "compositor/output_damage.hpp"

#include "output/model/mapping.hpp"
#include "render/software/multi_output_scene_renderer.hpp"

#include <algorithm>
#include <ranges>
#include <set>

namespace gw::compositor {
namespace {

using glasswyrm::output::DamageFilterFootprint;
using glasswyrm::output::LogicalRectangle;
using glasswyrm::output::OutputMapping;
using glasswyrm::output::OutputTransform;
using glasswyrm::output::PhysicalRectangle;
using glasswyrm::output::RationalScale;

OutputMapping mapping(const gwipc_output_upsert& output) noexcept {
  return {{output.logical_x, output.logical_y},
          {output.logical_width, output.logical_height},
          {output.physical_pixel_width, output.physical_pixel_height},
          {output.scale_numerator, output.scale_denominator},
          static_cast<OutputTransform>(output.transform)};
}

bool same_shape(const gwipc_output_upsert& left,
                const gwipc_output_upsert& right) noexcept {
  return left.enabled == right.enabled &&
         left.logical_x == right.logical_x &&
         left.logical_y == right.logical_y &&
         left.logical_width == right.logical_width &&
         left.logical_height == right.logical_height &&
         left.physical_pixel_width == right.physical_pixel_width &&
         left.physical_pixel_height == right.physical_pixel_height &&
         left.scale_numerator == right.scale_numerator &&
         left.scale_denominator == right.scale_denominator &&
         left.transform == right.transform;
}

bool same_surface(const gwipc_surface_upsert& left,
                  const gwipc_surface_upsert& right) noexcept {
  return left.logical_x == right.logical_x &&
         left.logical_y == right.logical_y &&
         left.logical_width == right.logical_width &&
         left.logical_height == right.logical_height &&
         left.stacking == right.stacking && left.visible == right.visible &&
         left.clipping == right.clipping && left.clip_x == right.clip_x &&
         left.clip_y == right.clip_y && left.clip_width == right.clip_width &&
         left.clip_height == right.clip_height &&
         left.opacity == right.opacity &&
         left.scale_numerator == right.scale_numerator &&
         left.scale_denominator == right.scale_denominator &&
         left.presentation_flags == right.presentation_flags;
}

std::optional<LogicalRectangle>
surface_bounds(const gwipc_surface_upsert& surface) noexcept {
  Rectangle local{0, 0, surface.logical_width, surface.logical_height};
  if (surface.clipping) {
    const auto clipped = intersection(
        local, {surface.clip_x, surface.clip_y, surface.clip_width,
                surface.clip_height});
    if (!clipped)
      return std::nullopt;
    local = *clipped;
  }
  const auto placed = translate(local, surface.logical_x, surface.logical_y);
  if (!placed)
    return std::nullopt;
  return LogicalRectangle{placed->x, placed->y, placed->width,
                          placed->height};
}

bool member_of(const Scene& scene, const std::uint64_t surface_id,
               const std::uint64_t output_id) noexcept {
  const auto membership = scene.surface_outputs.find(surface_id);
  return membership != scene.surface_outputs.end() &&
         std::ranges::find(membership->second.output_ids, output_id) !=
             membership->second.output_ids.end();
}

DamageFilterFootprint footprint(const gwipc_output_upsert& output,
                                const gwipc_surface_upsert& surface) noexcept {
  const auto filter = render::software::select_sampling_filter(
      RationalScale{output.scale_numerator, output.scale_denominator},
      surface.scale_numerator);
  return filter == render::software::SamplingFilter::Bilinear
             ? DamageFilterFootprint::Bilinear
             : DamageFilterFootprint::Point;
}

void add_surface(DamageRegion& damage, const Scene& scene,
                 const std::uint64_t surface_id,
                 const gwipc_output_upsert& output) {
  const auto surface = scene.surfaces.find(surface_id);
  if (surface == scene.surfaces.end() || !surface->second.visible ||
      !member_of(scene, surface_id, output.output_id))
    return;
  const auto bounds = surface_bounds(surface->second);
  if (!bounds)
    return;
  const auto native = glasswyrm::output::map_logical_damage_to_native(
      mapping(output), *bounds, footprint(output, surface->second));
  if (native)
    damage.add({static_cast<std::int32_t>(native->x),
                static_cast<std::int32_t>(native->y), native->width,
                native->height});
}

} // namespace

PhysicalOutputDamage calculate_output_damage(
    const Scene& before, const Scene& after,
    const std::span<const std::uint64_t> content_changed) {
  PhysicalOutputDamage result;
  std::set<std::uint64_t> surface_ids;
  for (const auto& [id, unused] : before.surfaces) {
    (void)unused;
    surface_ids.insert(id);
  }
  for (const auto& [id, unused] : after.surfaces) {
    (void)unused;
    surface_ids.insert(id);
  }
  surface_ids.insert(content_changed.begin(), content_changed.end());

  for (const auto& [output_id, output] : after.outputs) {
    if (!output.enabled)
      continue;
    DamageRegion damage(
        {0, 0, output.physical_pixel_width, output.physical_pixel_height});
    const auto previous_output = before.outputs.find(output_id);
    if (previous_output == before.outputs.end() ||
        !same_shape(previous_output->second, output)) {
      damage.add_full_output();
      result.emplace(output_id, damage.rectangles());
      continue;
    }
    for (const auto surface_id : surface_ids) {
      const auto old = before.surfaces.find(surface_id);
      const auto now = after.surfaces.find(surface_id);
      const auto old_membership = before.surface_outputs.find(surface_id);
      const auto new_membership = after.surface_outputs.find(surface_id);
      const bool changed_content =
          std::ranges::find(content_changed, surface_id) !=
          content_changed.end();
      const bool changed =
          changed_content || old == before.surfaces.end() ||
          now == after.surfaces.end() ||
          !same_surface(old->second, now->second) ||
          old_membership == before.surface_outputs.end() ||
          new_membership == after.surface_outputs.end() ||
          old_membership->second != new_membership->second;
      if (!changed)
        continue;
      add_surface(damage, before, surface_id, previous_output->second);
      add_surface(damage, after, surface_id, output);
    }
    if (!damage.rectangles().empty())
      result.emplace(output_id, damage.rectangles());
  }
  return result;
}

} // namespace gw::compositor
