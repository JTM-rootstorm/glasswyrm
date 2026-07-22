#include "compositor/scene.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace {

using gw::compositor::SceneModel;
using gw::compositor::SceneProfile;

void require(const bool condition, const char *message) {
  gw::test::require(condition, message);
}

gwipc_output_upsert output(const std::uint64_t id, const std::int32_t x,
                           const std::uint32_t physical_width,
                           const std::uint32_t physical_height,
                           const std::uint32_t scale_numerator = 1,
                           const std::uint32_t scale_denominator = 1) {
  gwipc_output_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = id;
  value.enabled = true;
  value.logical_x = x;
  value.logical_width =
      (physical_width * scale_denominator + scale_numerator - 1) /
      scale_numerator;
  value.logical_height =
      (physical_height * scale_denominator + scale_numerator - 1) /
      scale_numerator;
  value.physical_pixel_width = physical_width;
  value.physical_pixel_height = physical_height;
  value.refresh_millihertz = 60'000;
  value.scale_numerator = scale_numerator;
  value.scale_denominator = scale_denominator;
  value.transform = GWIPC_TRANSFORM_NORMAL;
  value.color = {GWIPC_SDR_COLOR_SPACE_SRGB,
                 GWIPC_TRANSFER_FUNCTION_SRGB,
                 GWIPC_COLOR_PRIMARIES_SRGB,
                 0,
                 0,
                 0,
                 0};
  return value;
}

gwipc_surface_upsert
surface(const std::uint64_t id, const std::uint64_t primary_output_id,
        const std::int32_t x, const std::int32_t y, const std::uint32_t width,
        const std::uint32_t height, const std::uint32_t client_scale = 1) {
  gwipc_surface_upsert value{};
  value.struct_size = sizeof(value);
  value.surface_id = id;
  value.output_id = primary_output_id;
  value.logical_x = x;
  value.logical_y = y;
  value.logical_width = width;
  value.logical_height = height;
  value.visible = true;
  value.transform = GWIPC_TRANSFORM_NORMAL;
  value.opacity = GWIPC_OPACITY_ONE;
  value.scale_numerator = client_scale;
  value.scale_denominator = 1;
  value.color = {GWIPC_SDR_COLOR_SPACE_SRGB,
                 GWIPC_TRANSFER_FUNCTION_SRGB,
                 GWIPC_COLOR_PRIMARIES_SRGB,
                 0,
                 0,
                 0,
                 0};
  return value;
}

gwipc_surface_output_state membership(
    const std::uint64_t surface_id, const std::uint64_t primary_output_id,
    const std::vector<std::uint64_t> &output_ids,
    const std::uint32_t preferred_numerator,
    const std::uint32_t preferred_denominator, const std::uint64_t generation,
    const std::uint32_t client_scale = 1) {
  gwipc_surface_output_state value{};
  value.struct_size = sizeof(value);
  value.surface_id = surface_id;
  value.primary_output_id = primary_output_id;
  value.output_ids = output_ids.data();
  value.output_count = output_ids.size();
  value.preferred_scale_numerator = preferred_numerator;
  value.preferred_scale_denominator = preferred_denominator;
  value.client_buffer_scale = client_scale;
  value.scale_mode = client_scale == 1 ? GWIPC_SURFACE_SCALE_LEGACY
                                       : GWIPC_SURFACE_SCALE_SCALED_PIXMAP;
  value.layout_generation = generation;
  return value;
}

gwipc_frame_commit frame(const std::uint64_t output_id = 0,
                         const std::uint64_t commit_id = 1,
                         const std::uint64_t generation = 7) {
  gwipc_frame_commit value{};
  value.struct_size = sizeof(value);
  value.commit_id = commit_id;
  value.output_id = output_id;
  value.producer_generation = generation;
  return value;
}

void stage_canonical_outputs(SceneModel &model,
                             const std::uint64_t generation = 9) {
  require(model.begin_complete_snapshot(1, generation),
          "M13 complete snapshot carries primary and generation");
  require(model.apply(output(1, 0, 640, 480)), "LEFT output stages");
  require(model.apply(output(2, 640, 800, 600, 5, 4)),
          "RIGHT fractional-scale output stages");
}

void test_historical_profile_infers_singleton_membership() {
  SceneModel model;
  require(model.begin_complete_snapshot(), "historical snapshot begins");
  require(model.apply(output(1, 0, 100, 80)), "historical output stages");
  require(model.apply(surface(7, 1, 2, 3, 20, 10)),
          "historical surface stages");
  const std::vector<std::uint64_t> ids{1};
  require(!model.apply(membership(7, 1, ids, 1, 1, 1)),
          "historical profile rejects explicit M13 membership");
  require(model.end_complete_snapshot(), "historical snapshot completes");
  require(model.pending().outputs.size() == 1 &&
              model.pending().outputs.contains(1) &&
              model.pending().surface_outputs.at(7).output_ids == ids,
          "historical singleton map and membership are inferred");
  require(model.commit(frame(1)).accepted(),
          "historical frame behavior remains accepted");
}

void test_canonical_spanning_membership_commits() {
  SceneModel model(SceneProfile::OutputModel);
  stage_canonical_outputs(model);
  require(model.apply(surface(10, 1, 600, 20, 100, 40)),
          "spanning surface stages");
  const std::vector<std::uint64_t> ids{1, 2};
  require(model.apply(membership(10, 1, ids, 1, 1, 9)),
          "sorted complete membership stages");
  require(model.end_complete_snapshot(), "valid M13 snapshot completes");
  require(!model.commit(frame(1)).accepted(),
          "multi-output commit requires output ID zero");
  require(model.commit(frame()).accepted(), "multi-output frame commits");
  require(model.committed().output == std::nullopt &&
              model.committed().outputs.size() == 2 &&
              model.committed().primary_output_id == 1 &&
              model.committed().configuration_generation == 9 &&
              model.committed().surface_outputs.at(10).output_ids == ids,
          "committed M13 scene owns output and membership maps");
}

void test_snapshot_requires_complete_current_membership() {
  SceneModel model(SceneProfile::OutputModel);
  stage_canonical_outputs(model);
  require(model.apply(surface(10, 1, 600, 20, 100, 40)),
          "surface stages without membership");
  require(!model.end_complete_snapshot(),
          "missing SurfaceOutputState keeps snapshot incomplete");
  const std::vector<std::uint64_t> wrong_order{2, 1};
  require(model.apply(membership(10, 1, wrong_order, 1, 1, 9)),
          "structurally valid unsorted membership stages");
  require(!model.end_complete_snapshot(),
          "membership ordering and geometry are validated exactly");
  model.abort_complete_snapshot();
  require(!model.initial_snapshot_received() && model.pending().outputs.empty(),
          "failed snapshot can be aborted atomically");
}

void test_snapshot_rejects_overlap_and_invalid_primary() {
  SceneModel overlap(SceneProfile::OutputModel);
  stage_canonical_outputs(overlap);
  require(overlap.apply(output(2, 639, 800, 600, 5, 4)),
          "replacement output stages for complete validation");
  require(!overlap.end_complete_snapshot(),
          "overlapping enabled outputs reject the snapshot");

  SceneModel primary(SceneProfile::OutputModel);
  require(primary.begin_complete_snapshot(99, 1) &&
              primary.apply(output(1, 0, 640, 480)),
          "unknown primary stages until snapshot validation");
  require(!primary.end_complete_snapshot(),
          "primary must name an enabled output");
}

void test_duplicate_and_stale_memberships_reject() {
  SceneModel duplicate(SceneProfile::OutputModel);
  stage_canonical_outputs(duplicate);
  require(duplicate.apply(surface(4, 2, 700, 10, 20, 20)),
          "RIGHT surface stages");
  const std::vector<std::uint64_t> duplicate_ids{2, 2};
  require(!duplicate.apply(membership(4, 2, duplicate_ids, 5, 4, 9)),
          "duplicate output IDs reject at record ingestion");

  const std::vector<std::uint64_t> right{2};
  require(duplicate.apply(membership(4, 2, right, 5, 4, 8)),
          "stale layout generation stages for snapshot validation");
  require(!duplicate.end_complete_snapshot(),
          "membership generation must match configuration generation");
}

void test_hidden_surface_may_have_empty_membership() {
  SceneModel model(SceneProfile::OutputModel);
  stage_canonical_outputs(model, 12);
  auto hidden = surface(20, 2, 700, 10, 30, 30, 2);
  hidden.visible = false;
  require(model.apply(hidden), "hidden scale-aware surface stages");
  const std::vector<std::uint64_t> none;
  require(model.apply(membership(20, 2, none, 5, 4, 12, 2)),
          "hidden surface carries assigned primary and empty membership");
  require(model.end_complete_snapshot() && model.commit(frame()).accepted(),
          "hidden empty membership is a valid complete scene");
}

void test_output_bound_is_enforced_during_staging() {
  SceneModel model(SceneProfile::OutputModel);
  require(model.begin_complete_snapshot(1, 1), "bounded snapshot begins");
  for (std::uint64_t id = 1; id <= GWIPC_MAXIMUM_OUTPUTS; ++id)
    require(model.apply(
                output(id, static_cast<std::int32_t>((id - 1) * 10), 10, 10)),
            "bounded output stages");
  require(!model.apply(output(GWIPC_MAXIMUM_OUTPUTS + 1, 80, 10, 10)),
          "ninth output rejects before map growth");
}

void test_commit_preserves_typed_local_damage() {
  SceneModel model(SceneProfile::OutputModel);
  require(model.begin_complete_snapshot(1, 9) &&
              model.apply(output(1, 0, 2560, 1440)) &&
              model.apply(surface(10, 1, 0, 0, 2560, 1440)),
          "exact-damage output scene stages");
  const std::vector<std::uint64_t> outputs{1};
  require(model.apply(membership(10, 1, outputs, 1, 1, 9)) &&
              model.end_complete_snapshot() &&
              model.commit(frame(0, 1, 1)).accepted(),
          "exact-damage output scene commits");

  const std::array rectangles{
      gwipc_damage_rectangle{100, 200, 64, 64},
      gwipc_damage_rectangle{-8, -4, 16, 12},
  };
  gwipc_surface_damage damage{};
  damage.struct_size = sizeof(damage);
  damage.surface_id = 10;
  damage.rectangles = rectangles.data();
  damage.rectangle_count = rectangles.size();
  require(model.apply(damage), "exact local damage stages");
  const auto committed = model.commit(frame(0, 2, 2));
  const auto& exact = committed.surface_damage.surfaces.at(10);
  require(committed.accepted() && exact.trusted_complete &&
              exact.fallback_reason ==
                  gw::compositor::SurfaceDamageFallbackReason::None &&
              exact.local_rectangles ==
                  std::vector<gw::compositor::Rectangle>{
                      {0, 0, 8, 8}, {100, 200, 64, 64}},
          "commit retains clipped normalized local rectangles with trust");
}

void test_damage_complexity_collapses_to_typed_full_surface() {
  SceneModel model(SceneProfile::OutputModel);
  require(model.begin_complete_snapshot(1, 3) &&
              model.apply(output(1, 0, 4096, 3)) &&
              model.apply(surface(10, 1, 0, 0, 4096, 3)),
          "complex-damage scene stages");
  const std::vector<std::uint64_t> outputs{1};
  require(model.apply(membership(10, 1, outputs, 1, 1, 3)) &&
              model.end_complete_snapshot() &&
              model.commit(frame(0, 1, 1)).accepted(),
          "complex-damage scene commits");

  std::vector<gwipc_damage_rectangle> rectangles;
  rectangles.reserve(GWIPC_MAXIMUM_DAMAGE_RECTANGLES);
  for (std::size_t index = 0; index < GWIPC_MAXIMUM_DAMAGE_RECTANGLES; ++index)
    rectangles.push_back(
        {static_cast<std::int32_t>(index * 2), 0, 1, 1});
  gwipc_surface_damage first{sizeof(first), 10, rectangles.data(),
                             rectangles.size(), {}};
  const gwipc_damage_rectangle extra{1, 2, 1, 1};
  gwipc_surface_damage second{sizeof(second), 10, &extra, 1, {}};
  require(model.apply(first) && model.apply(second),
          "bounded damage batches normalize together");
  const auto committed = model.commit(frame(0, 2, 2));
  const auto& collapsed = committed.surface_damage.surfaces.at(10);
  require(collapsed.trusted_complete &&
              collapsed.fallback_reason ==
                  gw::compositor::SurfaceDamageFallbackReason::RectangleLimit &&
              collapsed.local_rectangles ==
                  std::vector<gw::compositor::Rectangle>{{0, 0, 4096, 3}},
          "rectangle limit records a conservative typed full-surface fallback");
}

} // namespace

int main() {
  test_historical_profile_infers_singleton_membership();
  test_canonical_spanning_membership_commits();
  test_snapshot_requires_complete_current_membership();
  test_snapshot_rejects_overlap_and_invalid_primary();
  test_duplicate_and_stale_memberships_reject();
  test_hidden_surface_may_have_empty_membership();
  test_output_bound_is_enforced_during_staging();
  test_commit_preserves_typed_local_damage();
  test_damage_complexity_collapses_to_typed_full_surface();
}
