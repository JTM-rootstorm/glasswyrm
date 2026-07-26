#include "glasswyrmd/vrr_state_cache.hpp"
#include "tests/helpers/test_support.hpp"

#include <map>

using namespace glasswyrm;
using namespace glasswyrm::server;
using gw::test::require;

namespace {

constexpr std::uint64_t kOutputId = 7;
constexpr std::uint32_t kWindowId = 20;
constexpr std::uint64_t kSurfaceId = (UINT64_C(1) << 32U) | kWindowId;

output::OutputLayout layout() {
  output::OutputLayout value;
  output::OutputDescriptor descriptor;
  descriptor.id = output::OutputId{kOutputId};
  descriptor.name = "HEADLESS-1";
  descriptor.connected = true;
  descriptor.mode_configurable = true;
  descriptor.scale_configurable = true;
  descriptor.transform_configurable = true;
  descriptor.primary_eligible = true;
  descriptor.arbitrary_headless_mode = true;
  descriptor.supported_transform_mask = output::kAllOutputTransformsMask;
  descriptor.minimum_scale = {1, 1};
  descriptor.maximum_scale = {4, 1};
  descriptor.maximum_scale_denominator = 120;
  descriptor.maximum_physical_width = 4096;
  descriptor.maximum_physical_height = 4096;
  descriptor.maximum_physical_pixels = 16'777'216;
  descriptor.modes.push_back({output::OutputModeId{17}, descriptor.id, 800, 600,
                              60'000, 0, "800x600", true, true});
  value.descriptors.emplace(descriptor.id, descriptor);
  output::OutputState state;
  state.output_id = descriptor.id;
  state.mode_id = descriptor.modes.front().id;
  state.enabled = true;
  state.logical_width = state.physical_width = 800;
  state.logical_height = state.physical_height = 600;
  state.refresh_millihertz = 60'000;
  state.scale = {1, 1};
  state.primary = true;
  state.generation = 7;
  value.states.emplace(state.output_id, state);
  value.output_order = {state.output_id};
  value.primary_output_id = state.output_id;
  value.root_logical_width = 800;
  value.root_logical_height = 600;
  value.generation = 7;
  value.enabled_output_count = 1;
  require(static_cast<bool>(output::validate_layout(value)),
          "VRR readiness layout validates");
  return value;
}

gwipc_output_vrr_capability_upsert
capability(const std::uint64_t output_id = kOutputId) {
  gwipc_output_vrr_capability_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = output_id;
  value.connector_property_present = 1;
  value.hardware_capable = 1;
  value.kms_controllable = 1;
  value.simulated = 1;
  value.range_available = 1;
  value.minimum_refresh_millihertz = 40'000;
  value.maximum_refresh_millihertz = 144'000;
  value.reason_flags = GWIPC_VRR_REASON_SIMULATED_HEADLESS;
  return value;
}

gwipc_output_vrr_policy_upsert
policy(const std::uint64_t output_id = kOutputId) {
  gwipc_output_vrr_policy_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = output_id;
  value.mode = GWIPC_VRR_POLICY_FOCUSED;
  return value;
}

VrrStateCache output_ready_cache() {
  VrrStateCache cache;
  require(cache.replace_inventory({capability()}, {policy()}),
          "install readiness inventory");
  gwipc_policy_output_vrr_state policy_state{};
  policy_state.struct_size = sizeof(policy_state);
  policy_state.output_id = kOutputId;
  policy_state.mode = GWIPC_VRR_POLICY_FOCUSED;
  policy_state.candidate_required = 1;
  require(cache.stage_policy_result(7, {policy_state}, {}),
          "install output policy result");
  gwipc_output_vrr_state_upsert state{};
  state.struct_size = sizeof(state);
  state.output_id = kOutputId;
  state.requested_mode = GWIPC_VRR_POLICY_FOCUSED;
  state.decision = GWIPC_VRR_DECISION_DISABLED;
  state.session_active = 1;
  state.state_generation = 7;
  state.reason_flags =
      GWIPC_VRR_REASON_NO_CANDIDATE | GWIPC_VRR_REASON_SIMULATED_HEADLESS;
  require(cache.seed_compositor_state({state}, {}),
          "install output state without optional timing");
  return cache;
}

VrrStateCache window_ready_cache() {
  VrrStateCache cache;
  require(cache.replace_inventory({capability()}, {policy()}),
          "install window readiness inventory");
  cache.set_window_preference(kWindowId, GWIPC_VRR_PREFERENCE_PREFER);
  gwipc_policy_output_vrr_state output_state{};
  output_state.struct_size = sizeof(output_state);
  output_state.output_id = kOutputId;
  output_state.mode = GWIPC_VRR_POLICY_FOCUSED;
  output_state.selected_window_id = kWindowId;
  output_state.desired_enabled = 1;
  output_state.candidate_required = 1;
  gwipc_policy_window_vrr_state window_state{};
  window_state.struct_size = sizeof(window_state);
  window_state.window_id = kWindowId;
  window_state.output_id = kOutputId;
  window_state.preference = GWIPC_VRR_PREFERENCE_PREFER;
  window_state.selected = 1;
  window_state.eligible = 1;
  window_state.focused = 1;
  window_state.exclusive_output_membership = 1;
  require(cache.stage_policy_result(7, {output_state}, {window_state}),
          "install window policy result");
  gwipc_output_vrr_state_upsert output_effective{};
  output_effective.struct_size = sizeof(output_effective);
  output_effective.output_id = kOutputId;
  output_effective.requested_mode = GWIPC_VRR_POLICY_FOCUSED;
  output_effective.decision = GWIPC_VRR_DECISION_ENABLED;
  output_effective.desired_enabled = 1;
  output_effective.effective_enabled = 1;
  output_effective.session_active = 1;
  output_effective.candidate_window_id = kWindowId;
  output_effective.candidate_surface_id = kSurfaceId;
  output_effective.state_generation = 7;
  require(cache.seed_compositor_state({output_effective}, {}),
          "install selected output state");
  gwipc_surface_vrr_state surface{};
  surface.struct_size = sizeof(surface);
  surface.surface_id = kSurfaceId;
  surface.window_id = kWindowId;
  surface.output_id = kOutputId;
  surface.preference = GWIPC_VRR_PREFERENCE_PREFER;
  surface.policy_selected = 1;
  surface.policy_eligible = 1;
  surface.focused = 1;
  surface.exclusive_output_membership = 1;
  surface.policy_generation = 7;
  require(cache.stage_surface_states({surface}),
          "install selected surface state");
  return cache;
}

const std::map<std::uint64_t, gwipc_vrr_policy_mode> kPolicies{
    {kOutputId, GWIPC_VRR_POLICY_FOCUSED}};
const std::array<VrrQueryWindow, 1> kWindows{{{kWindowId, kSurfaceId}}};

void expect(const VrrQueryResult &result, const VrrQueryReadiness readiness,
            const VrrQueryReason reason, const char *detail) {
  require(result.readiness == readiness && result.reason == reason &&
              (readiness == VrrQueryReadiness::Ready) ==
                  result.snapshot.has_value(),
          detail);
}

void test_output_readiness_matrix() {
  const auto output_layout = layout();
  expect(project_vrr_query(nullptr, output_layout, kPolicies),
         VrrQueryReadiness::RetryableNotReady, VrrQueryReason::CacheUnavailable,
         "an absent cache is retryable and has no partial snapshot");

  VrrStateCache missing;
  require(missing.replace_inventory({capability(8)}, {policy(8)}),
          "install nonmatching inventory");
  expect(project_vrr_query(&missing, output_layout, kPolicies),
         VrrQueryReadiness::RetryableNotReady,
         VrrQueryReason::OutputCacheMissing,
         "a missing committed output is retryable");

  VrrStateCache incomplete;
  require(incomplete.replace_inventory({capability()}, {policy()}),
          "install incomplete inventory");
  expect(project_vrr_query(&incomplete, output_layout, kPolicies),
         VrrQueryReadiness::RetryableNotReady,
         VrrQueryReason::OutputStateMissing,
         "a missing compositor state is retryable");

  auto ready = output_ready_cache();
  const auto projected = project_vrr_query(&ready, output_layout, kPolicies);
  expect(projected, VrrQueryReadiness::Ready, VrrQueryReason::None,
         "missing timing remains a coherent snapshot");
  require(projected.snapshot->timings.empty() &&
              projected.snapshot->states.size() == 1,
          "timing omission is explicit inside an otherwise complete view");

  auto empty_scene_fallback = ready;
  gwipc_policy_output_vrr_state newer_policy{};
  newer_policy.struct_size = sizeof(newer_policy);
  newer_policy.output_id = kOutputId;
  newer_policy.mode = GWIPC_VRR_POLICY_FOCUSED;
  newer_policy.candidate_required = 1;
  require(empty_scene_fallback.stage_policy_result(8, {newer_policy}, {}),
          "advance an empty scene beyond its output-layout generation");
  expect(project_vrr_query(&empty_scene_fallback, output_layout, kPolicies),
         VrrQueryReadiness::Ready, VrrQueryReason::None,
         "an empty scene accepts compositor state stamped with the stable "
         "output-layout generation");

  auto stale = ready;
  auto &stale_outputs =
      const_cast<std::map<std::uint64_t, ServerVrrOutputState> &>(
          stale.outputs());
  stale_outputs.at(kOutputId).compositor_state->state_generation = 6;
  expect(project_vrr_query(&stale, output_layout, kPolicies),
         VrrQueryReadiness::RetryableNotReady, VrrQueryReason::OutputStateStale,
         "a stale output generation is retryable");

  auto policy_mismatch = ready;
  auto &policy_outputs =
      const_cast<std::map<std::uint64_t, ServerVrrOutputState> &>(
          policy_mismatch.outputs());
  policy_outputs.at(kOutputId).policy.mode = GWIPC_VRR_POLICY_OFF;
  expect(project_vrr_query(&policy_mismatch, output_layout, kPolicies),
         VrrQueryReadiness::RetryableNotReady,
         VrrQueryReason::CommittedPolicyIncoherent,
         "a cache policy awaiting promotion is retryable");

  auto bad_capability = ready;
  auto &bad_outputs =
      const_cast<std::map<std::uint64_t, ServerVrrOutputState> &>(
          bad_capability.outputs());
  bad_outputs.at(kOutputId).capability.output_id = 9;
  expect(project_vrr_query(&bad_capability, output_layout, kPolicies),
         VrrQueryReadiness::FatalInvariant,
         VrrQueryReason::OutputCapabilityIncoherent,
         "an impossible capability identity is fatal");

  expect(project_vrr_query(&ready, output_layout, {}),
         VrrQueryReadiness::FatalInvariant,
         VrrQueryReason::CommittedPolicyIncoherent,
         "an incomplete committed policy map is fatal");
}

void test_window_readiness_matrix() {
  const auto output_layout = layout();
  auto outputs_only = output_ready_cache();
  expect(project_vrr_query(&outputs_only, output_layout, kPolicies, kWindows),
         VrrQueryReadiness::RetryableNotReady,
         VrrQueryReason::WindowCacheMissing,
         "a missing queried window cache entry is retryable");

  auto ready = window_ready_cache();
  expect(project_vrr_query(&ready, output_layout, kPolicies, kWindows),
         VrrQueryReadiness::Ready, VrrQueryReason::None,
         "a coherent output and window projection is ready");

  auto inactive = ready;
  require(inactive.apply_session_state(GWIPC_SESSION_INACTIVE) ==
              VrrSessionStateStatus::Applied,
          "project an inactive session before querying");
  expect(project_vrr_query(&inactive, output_layout, kPolicies, kWindows),
         VrrQueryReadiness::Ready, VrrQueryReason::None,
         "an inactive compositor may disable a policy-selected candidate");
  require(inactive.outputs().at(kOutputId).policy_result->desired_enabled == 1 &&
              inactive.outputs()
                      .at(kOutputId)
                      .compositor_state->desired_enabled == 0,
          "inactive query preserves policy intent and compositor authority");

  auto unauthorized_enable = output_ready_cache();
  auto &unauthorized_outputs =
      const_cast<std::map<std::uint64_t, ServerVrrOutputState> &>(
          unauthorized_enable.outputs());
  unauthorized_outputs.at(kOutputId).compositor_state->desired_enabled = 1;
  expect(project_vrr_query(&unauthorized_enable, output_layout, kPolicies),
         VrrQueryReadiness::FatalInvariant,
         VrrQueryReason::OutputStateIncoherent,
         "a compositor cannot enable VRR without policy permission");

  auto missing_policy = ready;
  auto &policy_windows =
      const_cast<std::map<std::uint32_t, ServerVrrWindowState> &>(
          missing_policy.windows());
  policy_windows.at(kWindowId).policy_result.reset();
  expect(project_vrr_query(&missing_policy, output_layout, kPolicies, kWindows),
         VrrQueryReadiness::RetryableNotReady,
         VrrQueryReason::WindowPolicyMissing,
         "a missing window policy result is retryable");

  auto missing_state = ready;
  auto &state_windows =
      const_cast<std::map<std::uint32_t, ServerVrrWindowState> &>(
          missing_state.windows());
  state_windows.at(kWindowId).compositor_state.reset();
  expect(project_vrr_query(&missing_state, output_layout, kPolicies, kWindows),
         VrrQueryReadiness::RetryableNotReady,
         VrrQueryReason::WindowStateMissing,
         "a missing window compositor state is retryable");

  auto stale = ready;
  auto &stale_windows =
      const_cast<std::map<std::uint32_t, ServerVrrWindowState> &>(
          stale.windows());
  stale_windows.at(kWindowId).compositor_state->policy_generation = 6;
  expect(project_vrr_query(&stale, output_layout, kPolicies, kWindows),
         VrrQueryReadiness::RetryableNotReady, VrrQueryReason::WindowStateStale,
         "a stale window policy generation is retryable");

  auto incoherent = ready;
  auto &incoherent_windows =
      const_cast<std::map<std::uint32_t, ServerVrrWindowState> &>(
          incoherent.windows());
  incoherent_windows.at(kWindowId).compositor_state->surface_id = 99;
  expect(project_vrr_query(&incoherent, output_layout, kPolicies, kWindows),
         VrrQueryReadiness::FatalInvariant,
         VrrQueryReason::WindowStateIncoherent,
         "an impossible window-surface identity is fatal");
}

} // namespace

int main() {
  test_output_readiness_matrix();
  test_window_readiness_matrix();
}
