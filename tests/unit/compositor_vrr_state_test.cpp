#include "compositor/scene_vrr_validation.hpp"
#include "compositor/vrr_state.hpp"
#include "tests/helpers/test_support.hpp"

namespace {

gwipc_output_vrr_policy_upsert policy(const std::uint64_t output_id) {
  gwipc_output_vrr_policy_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = output_id;
  value.mode = GWIPC_VRR_POLICY_FULLSCREEN;
  return value;
}

gwipc_surface_vrr_state surface_state(const std::uint64_t surface_id,
                                      const std::uint32_t window_id,
                                      const std::uint64_t output_id) {
  gwipc_surface_vrr_state value{};
  value.struct_size = sizeof(value);
  value.surface_id = surface_id;
  value.window_id = window_id;
  value.output_id = output_id;
  value.preference = GWIPC_VRR_PREFERENCE_ALLOW;
  value.policy_selected = 1;
  value.policy_eligible = 1;
  value.focused = 1;
  value.fullscreen = 1;
  value.exclusive_output_membership = 1;
  value.policy_generation = 7;
  return value;
}

gw::compositor::Scene scene() {
  gw::compositor::Scene value;
  value.configuration_generation = 3;
  value.primary_output_id = 1;
  gwipc_output_upsert output{};
  output.output_id = 1;
  output.enabled = 1;
  value.outputs.emplace(1, output);
  gwipc_surface_upsert surface{};
  surface.surface_id = 10;
  surface.x11_window_id = 42;
  surface.output_id = 1;
  value.surfaces.emplace(10, surface);
  value.surface_outputs.emplace(
      10, gw::compositor::SurfaceOutputMembership{
              1, {1}, 1, 1, 1, GWIPC_SURFACE_SCALE_LEGACY, 3, 0});
  value.vrr.output_policies.emplace(1, policy(1));
  value.vrr.surfaces.emplace(10, surface_state(10, 42, 1));
  value.vrr.policy_generation = 7;
  return value;
}

} // namespace

int main() {
  using gw::test::require;
  auto valid = scene();
  const auto accepted = gw::compositor::validate_scene_vrr(valid, true);
  require(accepted.accepted() && accepted.policy_generation == 7,
          "complete M14 scene VRR state validates");
  require(!gw::compositor::validate_scene_vrr(valid, false).accepted(),
          "VRR records require explicit negotiation");

  auto missing_policy = valid;
  missing_policy.vrr.output_policies.clear();
  require(!gw::compositor::validate_scene_vrr(missing_policy, true).accepted(),
          "every M14 output requires one policy");

  auto duplicate = valid;
  auto second = duplicate.surfaces.at(10);
  second.surface_id = 11;
  second.x11_window_id = 43;
  duplicate.surfaces.emplace(11, second);
  duplicate.surface_outputs.emplace(
      11, gw::compositor::SurfaceOutputMembership{
              1, {1}, 1, 1, 1, GWIPC_SURFACE_SCALE_LEGACY, 3, 0});
  duplicate.vrr.surfaces.emplace(11, surface_state(11, 43, 1));
  require(!gw::compositor::validate_scene_vrr(duplicate, true).accepted(),
          "one output cannot select two VRR candidates");

  auto cursor = valid;
  cursor.surfaces.at(10).presentation_flags =
      GWIPC_SURFACE_PRESENTATION_CURSOR;
  cursor.surfaces.at(10).x11_window_id = 0;
  require(!gw::compositor::validate_scene_vrr(cursor, true).accepted(),
          "cursor carries no window VRR state");

  gw::compositor::CommittedVrrState committed;
  gwipc_output_vrr_state_upsert state{};
  state.output_id = 1;
  state.last_commit_id = 5;
  state.last_presented_generation = 9;
  gwipc_presentation_timing timing{};
  timing.output_id = 1;
  timing.commit_id = 5;
  timing.presented_generation = 9;
  std::string error;
  require(committed.promote({{1, state}}, {{1, timing}}, 5, 9, error),
          "matching state and timing promote atomically");
  const auto before = committed.outputs();
  timing.commit_id = 6;
  require(!committed.promote({{1, state}}, {{1, timing}}, 5, 9, error) &&
              committed.outputs().size() == before.size() &&
              committed.outputs().at(1).last_commit_id ==
                  before.at(1).last_commit_id &&
              committed.outputs().at(1).last_presented_generation ==
                  before.at(1).last_presented_generation,
          "invalid presentation result preserves committed VRR state");
}
