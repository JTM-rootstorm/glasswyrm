#include "glasswyrmd/lifecycle_projection.hpp"
#include "glasswyrmd/vrr_policy_projection.hpp"
#include "ipc/vrr_membership_hint.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <vector>

using namespace glasswyrm;
using namespace glasswyrm::server;
using gw::test::require;

namespace {

output::OutputLayout layout() {
  constexpr output::OutputId id{5};
  constexpr output::OutputModeId mode{6};
  output::OutputDescriptor descriptor;
  descriptor.id = id;
  descriptor.name = "HEADLESS-1";
  descriptor.connected = true;
  descriptor.mode_configurable = true;
  descriptor.scale_configurable = true;
  descriptor.primary_eligible = true;
  descriptor.arbitrary_headless_mode = true;
  descriptor.supported_transform_mask = output::kAllOutputTransformsMask;
  descriptor.minimum_scale = {1, 1};
  descriptor.maximum_scale = {4, 1};
  descriptor.maximum_scale_denominator = output::kMaximumScaleDenominator;
  descriptor.maximum_physical_width = output::kMaximumPhysicalExtent;
  descriptor.maximum_physical_height = output::kMaximumPhysicalExtent;
  descriptor.maximum_physical_pixels = output::kMaximumPhysicalPixels;
  descriptor.modes.push_back({mode, id, 800, 600, 60'000, 0, "800x600",
                              true, true});
  output::OutputState state;
  state.output_id = id;
  state.enabled = true;
  state.mode_id = mode;
  state.logical_width = state.physical_width = 800;
  state.logical_height = state.physical_height = 600;
  state.refresh_millihertz = 60'000;
  state.scale = {1, 1};
  state.primary = true;
  state.generation = 2;
  output::OutputLayout value;
  value.descriptors.emplace(id, std::move(descriptor));
  value.states.emplace(id, state);
  value.primary_output_id = id;
  value.root_logical_width = 800;
  value.root_logical_height = 600;
  value.generation = 2;
  value.enabled_output_count = 1;
  value.output_order = {id};
  return value;
}

VrrStateCache cache() {
  gwipc_output_vrr_capability_upsert capability{};
  capability.struct_size = sizeof(capability);
  capability.output_id = 5;
  capability.simulated = 1;
  capability.range_available = 1;
  capability.minimum_refresh_millihertz = 40'000;
  capability.maximum_refresh_millihertz = 60'000;
  gwipc_output_vrr_policy_upsert policy{};
  policy.struct_size = sizeof(policy);
  policy.output_id = 5;
  policy.mode = GWIPC_VRR_POLICY_APP_REQUESTED;
  VrrStateCache value;
  require(value.replace_inventory({capability}, {policy}),
          "install headless VRR inventory");
  return value;
}

void test_one_lifecycle_frame() {
  const auto outputs = layout();
  LifecycleSnapshot proposed;
  proposed.root_window = 1;
  proposed.workspace_id = 1;
  proposed.focused_window = 10;
  proposed.root_order = {10};
  LifecycleWindow window;
  window.xid = 10;
  window.parent = 1;
  window.requested_width = 640;
  window.requested_height = 480;
  window.assigned_output_id = 5;
  window.output_memberships = {5};
  proposed.windows.emplace(10, window);

  auto vrr = cache();
  VrrWindowStateStore published;
  published.ensure_window(10).preference = WindowVrrPreference::Prefer;
  synchronize_vrr_windows(proposed, published, vrr);
  const auto policy_submission = project_policy(proposed, 20, 30, &outputs,
                                                &vrr);
  const std::array canonical_outputs{UINT64_C(5)};
  const auto decoded_membership =
      ipc::internal::decode_vrr_membership_hint(
          canonical_outputs,
          policy_submission.output_hints.front().preferred_output_id);
  require(policy_submission.vrr.outputs.size() == 1 &&
              policy_submission.vrr.windows.size() == 1 &&
              decoded_membership == std::vector<std::uint64_t>({5}) &&
              policy_submission.output_hints.front().previous_output_id == 5,
          "ordinary M14 policy frame tags exact membership while preserving "
          "the assigned primary");

  PolicySnapshotResult policy_result;
  policy_result.generation = 30;
  gwipc_policy_window_state base{};
  base.struct_size = sizeof(base);
  base.window_id = 10;
  base.workspace_id = 1;
  base.output_id = 5;
  base.final_width = 640;
  base.final_height = 480;
  base.stacking = 0;
  base.visible = 1;
  base.focused = 1;
  base.managed = 1;
  base.fullscreen_eligible = GWIPC_TRI_STATE_TRUE;
  base.direct_scanout_eligible = GWIPC_TRI_STATE_FALSE;
  base.applied_state = GWIPC_POLICY_APPLIED_FULLSCREEN;
  policy_result.windows.push_back(base);
  gwipc_policy_output_vrr_state output{};
  output.struct_size = sizeof(output);
  output.output_id = 5;
  output.mode = GWIPC_VRR_POLICY_APP_REQUESTED;
  output.selected_window_id = 10;
  output.desired_enabled = 1;
  output.candidate_required = 1;
  policy_result.vrr_outputs.push_back(output);
  gwipc_policy_window_vrr_state selected{};
  selected.struct_size = sizeof(selected);
  selected.window_id = 10;
  selected.output_id = 5;
  selected.preference = GWIPC_VRR_PREFERENCE_PREFER;
  selected.selected = 1;
  selected.eligible = 1;
  selected.focused = 1;
  selected.fullscreen = 1;
  selected.exclusive_output_membership = 1;
  policy_result.vrr_windows.push_back(selected);

  const auto evaluated =
      apply_policy_result(proposed, policy_result, &outputs, &vrr);
  require(evaluated && vrr.generation() == 30,
          "policy result and lifecycle geometry validate together");
  const auto compositor =
      project_compositor(*evaluated, 21, 31, true, &outputs, &vrr);
  require(compositor.outputs.size() == 1 &&
              compositor.output_vrr_policies.size() == 1 &&
              compositor.surfaces.size() == 1 &&
              compositor.surface_vrr_states.size() == 1 &&
              compositor.surface_vrr_states.front().policy_generation == 30 &&
              compositor.surface_vrr_states.front().policy_selected == 1,
          "one compositor frame carries exact output and surface VRR state");
}

}  // namespace

int main() { test_one_lifecycle_frame(); }
