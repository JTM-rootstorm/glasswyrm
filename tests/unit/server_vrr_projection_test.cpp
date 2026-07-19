#include "glasswyrmd/vrr_events.hpp"
#include "glasswyrmd/vrr_policy_projection.hpp"
#include "tests/helpers/test_support.hpp"

#include <algorithm>

using namespace glasswyrm;
using namespace glasswyrm::server;
using gw::test::require;

namespace {

VrrStateCache cache() {
  gwipc_output_vrr_capability_upsert capability{};
  capability.struct_size = sizeof(capability);
  capability.output_id = 5;
  capability.hardware_capable = 1;
  capability.kms_controllable = 1;
  capability.connector_property_present = 1;
  gwipc_output_vrr_policy_upsert policy{};
  policy.struct_size = sizeof(policy);
  policy.output_id = 5;
  policy.mode = GWIPC_VRR_POLICY_FULLSCREEN;
  VrrStateCache result;
  require(result.replace_inventory({capability}, {policy}),
          "install projection inventory");
  return result;
}

LifecycleSnapshot snapshot() {
  LifecycleSnapshot value;
  value.root_window = 1;
  LifecycleWindow first;
  first.xid = 10;
  first.output_memberships = {5};
  first.assigned_output_id = 5;
  first.focused = true;
  first.policy_visible = true;
  first.applied_width = 100;
  first.applied_height = 100;
  value.windows.emplace(first.xid, first);
  auto second = first;
  second.xid = 20;
  second.focused = false;
  value.windows.emplace(second.xid, second);
  value.root_order = {10, 20};
  return value;
}

output::OutputLayout layout() {
  output::OutputLayout value;
  value.output_order = {output::OutputId{5}};
  value.states.emplace(output::OutputId{5}, output::OutputState{});
  return value;
}

std::pair<std::vector<gwipc_policy_output_vrr_state>,
          std::vector<gwipc_policy_window_vrr_state>>
policy_result() {
  gwipc_policy_output_vrr_state output{};
  output.struct_size = sizeof(output);
  output.output_id = 5;
  output.mode = GWIPC_VRR_POLICY_FULLSCREEN;
  output.selected_window_id = 10;
  output.desired_enabled = 1;
  output.candidate_required = 1;
  gwipc_policy_window_vrr_state selected{};
  selected.struct_size = sizeof(selected);
  selected.window_id = 10;
  selected.output_id = 5;
  selected.preference = GWIPC_VRR_PREFERENCE_PREFER;
  selected.selected = 1;
  selected.eligible = 1;
  selected.focused = 1;
  selected.exclusive_output_membership = 1;
  auto other = selected;
  other.window_id = 20;
  other.preference = GWIPC_VRR_PREFERENCE_DISABLE;
  other.selected = 0;
  other.eligible = 0;
  other.focused = 0;
  return {{output}, {selected, other}};
}

void test_projection_and_hash() {
  auto state = cache();
  auto proposed = snapshot();
  VrrWindowStateStore published;
  published.ensure_window(10).preference = WindowVrrPreference::Prefer;
  published.ensure_window(20).preference = WindowVrrPreference::Disable;
  synchronize_vrr_windows(proposed, published, state);
  const auto projected = project_vrr_policy(proposed, layout(), state);
  require(projected.outputs.size() == 1 && projected.windows.size() == 2 &&
              projected.memberships.at(10) == std::vector<std::uint64_t>{5},
          "policy projection carries exact output, window, and membership sets");
  auto [outputs, windows] = policy_result();
  require(validate_vrr_policy_result(projected, outputs, windows),
          "matching policy result validates");
  const auto hash = canonical_vrr_policy_hash(0x1234, projected, outputs,
                                              windows);
  std::ranges::reverse(windows);
  require(hash == canonical_vrr_policy_hash(0x1234, projected, outputs,
                                           windows),
          "canonical v4 hash is independent of peer record ordering");
  windows.front().selected = 1;
  require(!validate_vrr_policy_result(projected, outputs, windows),
          "two selected windows cannot pass exact validation");
}

void test_event_commit_boundary() {
  auto state = cache();
  auto proposed = snapshot();
  VrrWindowStateStore published;
  published.ensure_window(10).preference = WindowVrrPreference::Prefer;
  published.ensure_window(10).event_selections.emplace(77,
                                                        kKnownVrrEventMask);
  published.ensure_window(20).preference = WindowVrrPreference::Disable;
  synchronize_vrr_windows(proposed, published, state);
  auto [outputs, windows] = policy_result();
  require(state.stage_policy_result(3, outputs, windows),
          "stage policy state for event projection");
  gwipc_output_vrr_state_upsert effective{};
  effective.struct_size = sizeof(effective);
  effective.output_id = 5;
  effective.requested_mode = GWIPC_VRR_POLICY_FULLSCREEN;
  effective.decision = GWIPC_VRR_DECISION_ENABLED;
  effective.effective_enabled = 1;
  effective.state_generation = 4;
  require(state.seed_compositor_state({effective}, {}),
          "stage effective output state");
  const auto batch = prepare_vrr_event_batch(state, published, {{5, 500}});
  require(!published.find_output(500) &&
              !published.find_window(10)->effective_output_enabled,
          "event preparation has no observable side effects");
  apply_vrr_event_batch(published, batch);
  require(published.find_output(500)->effective_enabled &&
              published.find_window(10)->effective_output_enabled &&
              published.find_window(10)->event_selections.contains(77),
          "commit publishes output and window state while preserving selects");
}

}  // namespace

int main() {
  test_projection_and_hash();
  test_event_commit_boundary();
}
