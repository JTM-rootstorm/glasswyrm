#include "glasswyrmd/lifecycle_projection.hpp"
#include "glasswyrmd/vrr_events.hpp"
#include "glasswyrmd/vrr_policy_projection.hpp"
#include "ipc/vrr_membership_hint.hpp"
#include "tests/helpers/test_support.hpp"

#include <algorithm>
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

void test_membership_reconciliation_before_compositor() {
  const auto outputs = layout();
  LifecycleSnapshot proposed;
  proposed.root_window = 1;
  proposed.workspace_id = 1;
  proposed.root_order = {10};
  LifecycleWindow window;
  window.xid = 10;
  window.parent = 1;
  window.requested_width = 640;
  window.requested_height = 480;
  window.map_requested = true;
  proposed.windows.emplace(window.xid, window);

  auto vrr = cache();
  require(vrr.set_policy(5, GWIPC_VRR_POLICY_FOCUSED),
          "select focused VRR policy for membership reconciliation");
  VrrWindowStateStore published;
  synchronize_vrr_windows(proposed, published, vrr);

  const auto first_submission =
      project_policy(proposed, 20, 30, &outputs, &vrr);
  const std::array canonical_outputs{UINT64_C(5)};
  require(ipc::internal::decode_vrr_membership_hint(
              canonical_outputs,
              first_submission.output_hints.front().preferred_output_id) ==
              std::vector<std::uint64_t>{},
          "first mapped-window policy pass carries the pre-placement empty "
          "membership fact");

  PolicySnapshotResult first_result;
  first_result.generation = 30;
  gwipc_policy_window_state placed{};
  placed.struct_size = sizeof(placed);
  placed.window_id = 10;
  placed.workspace_id = 1;
  placed.output_id = 5;
  placed.final_width = 640;
  placed.final_height = 480;
  placed.stacking = 0;
  placed.visible = 1;
  placed.focused = 1;
  placed.managed = 1;
  placed.fullscreen_eligible = GWIPC_TRI_STATE_FALSE;
  placed.direct_scanout_eligible = GWIPC_TRI_STATE_FALSE;
  placed.applied_state = GWIPC_POLICY_APPLIED_NORMAL;
  first_result.windows.push_back(placed);
  gwipc_policy_output_vrr_state unavailable{};
  unavailable.struct_size = sizeof(unavailable);
  unavailable.output_id = 5;
  unavailable.mode = GWIPC_VRR_POLICY_FOCUSED;
  unavailable.candidate_required = 1;
  unavailable.reason_flags = GWIPC_VRR_REASON_NO_CANDIDATE;
  first_result.vrr_outputs.push_back(unavailable);
  gwipc_policy_window_vrr_state invalid_membership{};
  invalid_membership.struct_size = sizeof(invalid_membership);
  invalid_membership.window_id = 10;
  invalid_membership.output_id = 5;
  invalid_membership.preference = GWIPC_VRR_PREFERENCE_DEFAULT;
  invalid_membership.focused = 1;
  invalid_membership.reason_flags =
      GWIPC_VRR_REASON_SURFACE_MEMBERSHIP_INVALID;
  first_result.vrr_windows.push_back(invalid_membership);

  const auto first_evaluated =
      apply_policy_result(proposed, first_result, &outputs, &vrr);
  require(first_evaluated &&
              first_evaluated->windows.at(10).output_memberships ==
                  std::vector<std::uint64_t>{5} &&
              !policy_output_facts_match(proposed, *first_evaluated),
          "first GWM placement derives membership and requires reconciliation");

  const auto second_submission =
      project_policy(*first_evaluated, 21, 31, &outputs, &vrr);
  require(ipc::internal::decode_vrr_membership_hint(
              canonical_outputs,
              second_submission.output_hints.front().preferred_output_id) ==
              std::vector<std::uint64_t>{5},
          "reconciliation policy pass carries the derived membership fact");

  PolicySnapshotResult second_result;
  second_result.generation = 31;
  second_result.windows.push_back(placed);
  gwipc_policy_output_vrr_state available = unavailable;
  available.selected_window_id = 10;
  available.desired_enabled = 1;
  available.reason_flags = 0;
  second_result.vrr_outputs.push_back(available);
  gwipc_policy_window_vrr_state selected = invalid_membership;
  selected.selected = 1;
  selected.eligible = 1;
  selected.exclusive_output_membership = 1;
  selected.reason_flags = 0;
  second_result.vrr_windows.push_back(selected);

  const auto stable =
      apply_policy_result(*first_evaluated, second_result, &outputs, &vrr);
  require(stable && policy_output_facts_match(*first_evaluated, *stable),
          "second GWM result is membership-stable before compositor publish");
  const auto compositor =
      project_compositor(*stable, 22, 32, true, &outputs, &vrr);
  require(compositor.surface_vrr_states.size() == 1 &&
              compositor.surface_vrr_states.front().policy_selected == 1 &&
              compositor.surface_vrr_states.front()
                  .exclusive_output_membership == 1,
          "only the stable reconciled policy result is compositor-ready");
}

void test_override_redirect_surface_projection() {
  const auto outputs = layout();
  LifecycleSnapshot proposed;
  proposed.root_window = 1;
  proposed.workspace_id = 1;
  proposed.focused_window = 10;
  proposed.root_order = {10, 20};
  LifecycleWindow managed;
  managed.xid = 10;
  managed.parent = 1;
  managed.requested_width = managed.applied_width = 640;
  managed.requested_height = managed.applied_height = 480;
  managed.assigned_output_id = 5;
  managed.output_memberships = {5};
  managed.policy_visible = true;
  managed.focused = true;
  proposed.windows.emplace(managed.xid, managed);
  auto unmanaged = managed;
  unmanaged.xid = 20;
  unmanaged.override_redirect = true;
  unmanaged.focused = false;
  proposed.windows.emplace(unmanaged.xid, unmanaged);

  auto vrr = cache();
  VrrWindowStateStore published;
  synchronize_vrr_windows(proposed, published, vrr);
  const auto policy_submission =
      project_policy(proposed, 40, 50, &outputs, &vrr);
  require(policy_submission.vrr.windows.size() == 1 &&
              policy_submission.vrr.windows.front().window_id == 10 &&
              vrr.windows().size() == 2 &&
              !vrr.windows().at(20).policy_candidate,
          "override-redirect is tracked for publication but omitted from the "
          "GWM VRR candidate cardinality");

  PolicySnapshotResult policy_result;
  policy_result.generation = 50;
  for (const auto window_id : {UINT32_C(10), UINT32_C(20)}) {
    gwipc_policy_window_state base{};
    base.struct_size = sizeof(base);
    base.window_id = window_id;
    base.workspace_id = 1;
    base.output_id = 5;
    base.final_width = 640;
    base.final_height = 480;
    base.stacking = window_id == 10 ? 0 : 1;
    base.visible = 1;
    base.focused = window_id == 10;
    base.managed = window_id == 10;
    base.override_redirect = window_id == 20;
    base.fullscreen_eligible = GWIPC_TRI_STATE_FALSE;
    base.direct_scanout_eligible = GWIPC_TRI_STATE_FALSE;
    base.applied_state = GWIPC_POLICY_APPLIED_NORMAL;
    policy_result.windows.push_back(base);
  }
  gwipc_policy_output_vrr_state output{};
  output.struct_size = sizeof(output);
  output.output_id = 5;
  output.mode = GWIPC_VRR_POLICY_APP_REQUESTED;
  output.candidate_required = 1;
  output.reason_flags = GWIPC_VRR_REASON_NO_CANDIDATE;
  policy_result.vrr_outputs.push_back(output);
  gwipc_policy_window_vrr_state candidate{};
  candidate.struct_size = sizeof(candidate);
  candidate.window_id = 10;
  candidate.output_id = 5;
  candidate.preference = GWIPC_VRR_PREFERENCE_DEFAULT;
  candidate.focused = 1;
  candidate.exclusive_output_membership = 1;
  candidate.reason_flags = GWIPC_VRR_REASON_WINDOW_DID_NOT_REQUEST;
  policy_result.vrr_windows.push_back(candidate);

  const auto evaluated =
      apply_policy_result(proposed, policy_result, &outputs, &vrr);
  require(evaluated && !vrr.windows().at(20).policy_result,
          "exact GWM result cardinality excludes override-redirect without "
          "poisoning the server cache");
  const auto compositor =
      project_compositor(*evaluated, 41, 51, true, &outputs, &vrr);
  const auto override_state = std::ranges::find_if(
      compositor.surface_vrr_states,
      [](const auto& state) { return state.window_id == 20; });
  require(compositor.surface_vrr_states.size() == 2 &&
              override_state != compositor.surface_vrr_states.end() &&
              !override_state->policy_selected &&
              !override_state->policy_eligible &&
              (override_state->reason_flags &
               GWIPC_VRR_REASON_WINDOW_UNMANAGED) != 0,
          "override-redirect still receives the required safe nonmetadata "
          "SurfaceVrrState");
  const auto events = prepare_vrr_event_batch(vrr, published, {{5, 500}});
  const auto override_event = std::ranges::find_if(
      events.windows,
      [](const auto& transition) { return transition.window_id == 20; });
  require(events.windows.size() == 2 &&
              override_event != events.windows.end() &&
              override_event->after.primary_output == 500 &&
              (override_event->after.reason_flags &
               GWIPC_VRR_REASON_WINDOW_UNMANAGED) != 0,
          "override-redirect cache state remains publishable at the commit "
          "boundary without a GWM result");
}

void test_semantic_invalid_checkpoint_restore() {
  auto current = cache();
  LifecycleSnapshot proposed;
  LifecycleWindow window;
  window.xid = 10;
  window.assigned_output_id = 5;
  window.output_memberships = {5};
  proposed.windows.emplace(window.xid, window);
  VrrWindowStateStore published;
  synchronize_vrr_windows(proposed, published, current);
  const auto before = current;

  published.ensure_window(10).preference = WindowVrrPreference::Prefer;
  synchronize_vrr_windows(proposed, published, current);
  require(current.set_policy(5, GWIPC_VRR_POLICY_OFF) &&
              current.windows().at(10).preference ==
                  GWIPC_VRR_PREFERENCE_PREFER,
          "proposed lifecycle mutates the staged VRR cache");
  restore_vrr_lifecycle_checkpoint(current, before);
  require(current.outputs().at(5).policy.mode ==
              GWIPC_VRR_POLICY_APP_REQUESTED &&
              current.windows().at(10).preference ==
                  GWIPC_VRR_PREFERENCE_DEFAULT &&
              current.windows().at(10).policy_candidate,
          "semantic-invalid GWM result restores the exact pre-lifecycle VRR "
          "checkpoint before rejection completion");
}

}  // namespace

int main() {
  test_one_lifecycle_frame();
  test_membership_reconciliation_before_compositor();
  test_override_redirect_surface_projection();
  test_semantic_invalid_checkpoint_restore();
}
