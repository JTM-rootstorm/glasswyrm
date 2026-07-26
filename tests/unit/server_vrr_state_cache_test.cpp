#include "glasswyrmd/vrr_state_cache.hpp"
#include "tests/helpers/test_support.hpp"

#include <limits>

using glasswyrm::server::VrrResponseBatch;
using glasswyrm::server::VrrResponseExpectation;
using glasswyrm::server::VrrResponseStatus;
using glasswyrm::server::VrrSessionStateStatus;
using glasswyrm::server::VrrStateCache;
using gw::test::require;

namespace {

gwipc_output_vrr_capability_upsert capability(
    const std::uint64_t output_id = 7) {
  gwipc_output_vrr_capability_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = output_id;
  value.connector_property_present = 1;
  value.hardware_capable = 1;
  value.kms_controllable = 1;
  value.range_available = 1;
  value.atomic_required = 1;
  value.minimum_refresh_millihertz = 40'000;
  value.maximum_refresh_millihertz = 144'000;
  return value;
}

gwipc_output_vrr_policy_upsert policy(const std::uint64_t output_id = 7) {
  gwipc_output_vrr_policy_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = output_id;
  value.mode = GWIPC_VRR_POLICY_FOCUSED;
  return value;
}

VrrResponseBatch response() {
  VrrResponseBatch batch;
  gwipc_output_vrr_state_upsert state{};
  state.struct_size = sizeof(state);
  state.output_id = 7;
  state.requested_mode = GWIPC_VRR_POLICY_FOCUSED;
  state.decision = GWIPC_VRR_DECISION_ENABLED;
  state.desired_enabled = 1;
  state.effective_enabled = 1;
  state.property_readback_valid = 1;
  state.session_active = 1;
  state.state_generation = 9;
  state.last_commit_id = 41;
  state.last_presented_generation = 13;
  state.last_interval_nanoseconds = 8'333'333;
  batch.output_states.push_back(state);

  gwipc_presentation_timing timing{};
  timing.struct_size = sizeof(timing);
  timing.output_id = 7;
  timing.commit_id = 41;
  timing.presented_generation = 13;
  timing.effective_vrr_enabled = 1;
  timing.timestamp_available = 1;
  timing.interval_nanoseconds = 8'333'333;
  batch.timings.push_back(timing);
  batch.released_buffer_ids = {99};

  gwipc_frame_acknowledged ack{};
  ack.struct_size = sizeof(ack);
  ack.commit_id = 41;
  ack.presented_generation = 13;
  ack.result = GWIPC_FRAME_ACCEPTED;
  batch.acknowledgement = ack;
  return batch;
}

void test_exact_atomic_response() {
  VrrStateCache cache;
  require(cache.replace_inventory({capability()}, {policy()}),
          "install exact VRR inventory");
  require(cache.expect_response({41, 13, {7}, {99}}),
          "stage exact response expectation");
  auto batch = response();
  auto invalid = batch;
  invalid.timings.clear();
  require(cache.preflight(invalid) == VrrResponseStatus::TimingCountMismatch &&
              !cache.outputs().at(7).compositor_state,
          "missing timing rejects without partial promotion");
  invalid = batch;
  invalid.released_buffer_ids = {100};
  require(cache.promote(invalid) == VrrResponseStatus::ReleaseMismatch &&
              cache.expectation() != nullptr &&
              !cache.outputs().at(7).compositor_state,
          "release mismatch preserves the staged transaction");
  require(cache.promote(batch) == VrrResponseStatus::Accepted &&
              cache.expectation() == nullptr &&
              cache.outputs().at(7).compositor_state->effective_enabled == 1 &&
              cache.outputs().at(7).timing->interval_nanoseconds == 8'333'333,
          "exact response promotes state and timing together");
}

void test_policy_and_window_staging() {
  VrrStateCache cache;
  require(cache.replace_inventory({capability()}, {policy()}),
          "install policy inventory");
  cache.set_window_preference(20, GWIPC_VRR_PREFERENCE_PREFER);
  gwipc_policy_output_vrr_state output{};
  output.struct_size = sizeof(output);
  output.output_id = 7;
  output.mode = GWIPC_VRR_POLICY_FOCUSED;
  output.selected_window_id = 20;
  output.desired_enabled = 1;
  output.candidate_required = 1;
  gwipc_policy_window_vrr_state window{};
  window.struct_size = sizeof(window);
  window.window_id = 20;
  window.output_id = 7;
  window.preference = GWIPC_VRR_PREFERENCE_PREFER;
  window.selected = 1;
  window.eligible = 1;
  window.focused = 1;
  window.exclusive_output_membership = 1;
  require(cache.stage_policy_result(17, {output}, {window}) &&
              cache.generation() == 17,
          "policy result promotes exact output and window sets");
  gwipc_output_vrr_state_upsert effective{};
  effective.struct_size = sizeof(effective);
  effective.output_id = 7;
  effective.requested_mode = GWIPC_VRR_POLICY_FOCUSED;
  effective.decision = GWIPC_VRR_DECISION_DISABLED;
  effective.state_generation = 4;
  require(cache.seed_compositor_state({effective}, {}),
          "seed committed effective state before lifecycle staging");
  const auto checkpoint = cache;
  cache.set_window_preference(20, GWIPC_VRR_PREFERENCE_DISABLE);
  require(!cache.windows().at(20).policy_result,
          "staged preference invalidates only the working policy result");
  cache = checkpoint;
  require(cache.windows().at(20).preference == GWIPC_VRR_PREFERENCE_PREFER &&
              cache.windows().at(20).policy_result &&
              cache.outputs().at(7).compositor_state,
          "lifecycle rejection restores preference and effective state exactly");
  window.output_id = 8;
  require(!cache.stage_policy_result(18, {output}, {window}) &&
              cache.generation() == 17,
          "invalid policy result cannot partially replace committed state");
}

gwipc_policy_output_vrr_state policy_result(
    const std::uint64_t output_id, const std::uint32_t window_id = 0) {
  gwipc_policy_output_vrr_state value{};
  value.struct_size = sizeof(value);
  value.output_id = output_id;
  value.mode = GWIPC_VRR_POLICY_FOCUSED;
  value.selected_window_id = window_id;
  value.desired_enabled = window_id != 0;
  value.candidate_required = 1;
  return value;
}

gwipc_output_vrr_state_upsert active_state(
    const std::uint64_t output_id, const std::uint32_t window_id,
    const std::uint64_t serial) {
  gwipc_output_vrr_state_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = output_id;
  value.requested_mode = GWIPC_VRR_POLICY_FOCUSED;
  value.decision = GWIPC_VRR_DECISION_ENABLED;
  value.desired_enabled = 1;
  value.effective_enabled = 1;
  value.property_readback_valid = 1;
  value.session_active = 1;
  value.candidate_window_id = window_id;
  value.candidate_surface_id =
      (UINT64_C(1) << 32U) | window_id;
  value.reason_flags = GWIPC_VRR_REASON_HARDWARE_BEHAVIOR_UNCONFIRMED;
  value.state_generation = 17;
  value.transition_serial = serial;
  value.last_commit_id = 41;
  value.last_presented_generation = 13;
  value.last_flip_sequence = 23;
  value.last_flip_timestamp_nanoseconds = 1'000'000;
  value.last_interval_nanoseconds = 8'333'333;
  return value;
}

gwipc_presentation_timing active_timing(const std::uint64_t output_id) {
  gwipc_presentation_timing value{};
  value.struct_size = sizeof(value);
  value.output_id = output_id;
  value.commit_id = 41;
  value.presented_generation = 13;
  value.flip_sequence = 23;
  value.kernel_timestamp_nanoseconds = 1'000'000;
  value.interval_nanoseconds = 8'333'333;
  value.effective_vrr_enabled = 1;
  value.timestamp_available = 1;
  return value;
}

VrrStateCache active_session_cache() {
  VrrStateCache cache;
  require(cache.replace_inventory({capability()}, {policy()}),
          "install session cache inventory");
  cache.set_window_preference(20, GWIPC_VRR_PREFERENCE_PREFER);
  auto output = policy_result(7, 20);
  gwipc_policy_window_vrr_state window{};
  window.struct_size = sizeof(window);
  window.window_id = 20;
  window.output_id = 7;
  window.preference = GWIPC_VRR_PREFERENCE_PREFER;
  window.selected = 1;
  window.eligible = 1;
  window.focused = 1;
  window.exclusive_output_membership = 1;
  require(cache.stage_policy_result(17, {output}, {window}),
          "install current session policy result");
  require(cache.seed_compositor_state({active_state(7, 20, 5)},
                                      {active_timing(7)}),
          "install active compositor truth");
  return cache;
}

void test_compositor_may_block_policy_desire() {
  auto cache = active_session_cache();
  require(cache.expect_response({41, 13, {7}, {99}}),
          "stage response expectation for compositor rejection");
  auto batch = response();
  auto& state = batch.output_states.front();
  state.decision = GWIPC_VRR_DECISION_DISABLED;
  state.desired_enabled = 0;
  state.effective_enabled = 0;
  state.session_active = 0;
  state.candidate_window_id = 20;
  state.candidate_surface_id =
      (UINT64_C(1) << 32U) | UINT64_C(20);
  state.reason_flags =
      GWIPC_VRR_REASON_SESSION_INACTIVE | GWIPC_VRR_REASON_VT_SUSPENDED;
  batch.timings.front().effective_vrr_enabled = 0;
  require(cache.promote(batch) == VrrResponseStatus::Accepted &&
              cache.outputs().at(7).policy_result->desired_enabled == 1 &&
              cache.outputs().at(7).compositor_state->desired_enabled == 0,
          "compositor blockers override policy desire without changing policy");

  VrrStateCache forbidden;
  require(forbidden.replace_inventory({capability()}, {policy()}) &&
              forbidden.stage_policy_result(
                  9, {policy_result(7)}, {}) &&
              forbidden.expect_response({41, 13, {7}, {99}}),
          "install disabled policy before forbidden compositor enable");
  require(forbidden.preflight(response()) ==
              VrrResponseStatus::InvalidOutputState,
          "compositor cannot enable VRR without policy permission");
}

void test_inactive_session_projection() {
  auto cache = active_session_cache();
  require(cache.expect_response({51, 19, {7}, {101}}),
          "preserve an unrelated frame response expectation");
  require(cache.apply_session_state(GWIPC_SESSION_INACTIVE) ==
              VrrSessionStateStatus::Applied,
          "accepted inactive state projects into the server cache");
  const auto& output = cache.outputs().at(7);
  const auto& state = *output.compositor_state;
  constexpr std::uint64_t required_reasons =
      GWIPC_VRR_REASON_SESSION_INACTIVE | GWIPC_VRR_REASON_VT_SUSPENDED |
      GWIPC_VRR_REASON_TIMING_UNAVAILABLE;
  require(state.decision == GWIPC_VRR_DECISION_DISABLED &&
              state.desired_enabled == 0 && state.effective_enabled == 0 &&
              state.session_active == 0 && state.candidate_window_id == 20 &&
              state.candidate_surface_id ==
                  ((UINT64_C(1) << 32U) | UINT64_C(20)) &&
              (state.reason_flags & required_reasons) == required_reasons &&
              (state.reason_flags &
               GWIPC_VRR_REASON_HARDWARE_BEHAVIOR_UNCONFIRMED) != 0 &&
              state.state_generation == 17 && state.transition_serial == 6,
          "inactive truth preserves policy desire while disabling delivery");
  require(output.timing && output.timing->commit_id == 41 &&
              output.timing->presented_generation == 13 &&
              output.timing->effective_vrr_enabled == 0 &&
              output.timing->interval_nanoseconds == 8'333'333 &&
              cache.expectation() && cache.expectation()->commit_id == 51,
          "inactive timing stays historically coherent and expectations live");
  require(cache.apply_session_state(GWIPC_SESSION_INACTIVE) ==
                  VrrSessionStateStatus::Applied &&
              cache.outputs().at(7).compositor_state->transition_serial == 6,
          "repeated inactive truth is idempotent");
}

void test_active_session_invalidation() {
  auto cache = active_session_cache();
  require(cache.apply_session_state(GWIPC_SESSION_ACTIVE) ==
                  VrrSessionStateStatus::Applied &&
              !cache.outputs().at(7).compositor_state &&
              !cache.outputs().at(7).timing &&
              cache.outputs().at(7).policy_result &&
              cache.windows().at(20).policy_result,
          "accepted active state waits for ordinary compositor presentation");

  auto pending = active_session_cache();
  require(pending.expect_response({41, 13, {7}, {99}}),
          "install in-flight inactive presentation");
  require(pending.apply_session_state(GWIPC_SESSION_ACTIVE) ==
                  VrrSessionStateStatus::Applied &&
              pending.outputs().at(7).compositor_state &&
              pending.expectation(),
          "active transition retains truth required by an in-flight response");
  auto batch = response();
  batch.output_states.front().candidate_window_id = 20;
  batch.output_states.front().candidate_surface_id =
      (UINT64_C(1) << 32U) | UINT64_C(20);
  require(pending.promote(batch) == VrrResponseStatus::Accepted &&
              !pending.outputs().at(7).compositor_state &&
              !pending.outputs().at(7).timing && !pending.expectation(),
          "in-flight response completes before active invalidation");

  auto cancelled = active_session_cache();
  require(cancelled.expect_response({41, 13, {7}, {99}}),
          "install cancellable inactive presentation");
  require(cancelled.apply_session_state(GWIPC_SESSION_ACTIVE) ==
              VrrSessionStateStatus::Applied,
          "defer active invalidation for cancellable response");
  cancelled.cancel_expectation();
  require(!cancelled.outputs().at(7).compositor_state &&
              !cancelled.outputs().at(7).timing &&
              !cancelled.expectation(),
          "cancelling the response completes deferred active invalidation");
}

void test_session_transition_failures_are_atomic() {
  VrrStateCache cache;
  require(cache.replace_inventory({capability(7), capability(8)},
                                  {policy(7), policy(8)}),
          "install multi-output session inventory");
  auto first_policy = policy_result(7);
  auto second_policy = policy_result(8);
  require(cache.stage_policy_result(17, {first_policy, second_policy}, {}),
          "install multi-output policy desire");
  auto first = active_state(7, 20, 5);
  auto second = active_state(
      8, 30, std::numeric_limits<std::uint64_t>::max());
  require(cache.seed_compositor_state({first, second}, {}),
          "install multi-output active truth");
  require(cache.apply_session_state(GWIPC_SESSION_INACTIVE) ==
                  VrrSessionStateStatus::TransitionSerialExhausted &&
              cache.outputs().at(7).compositor_state->session_active == 1 &&
              cache.outputs().at(7).compositor_state->transition_serial == 5 &&
              cache.outputs().at(8).compositor_state->effective_enabled == 1,
          "serial overflow rejects every output without partial mutation");

  auto incoherent = active_session_cache();
  auto& outputs =
      const_cast<std::map<std::uint64_t,
                          glasswyrm::server::ServerVrrOutputState>&>(
          incoherent.outputs());
  outputs.at(7).timing->effective_vrr_enabled = 0;
  require(incoherent.apply_session_state(GWIPC_SESSION_INACTIVE) ==
                  VrrSessionStateStatus::OutputStateIncoherent &&
              incoherent.outputs().at(7).compositor_state->session_active == 1,
          "incoherent timing fails before mutating compositor state");

  const auto before = active_session_cache();
  auto invalid = before;
  require(invalid.apply_session_state(
              static_cast<gwipc_session_state>(99)) ==
                  VrrSessionStateStatus::InvalidState &&
              invalid.outputs().at(7).compositor_state->effective_enabled ==
                  before.outputs().at(7).compositor_state->effective_enabled,
          "invalid session state cannot mutate the cache");
}

}  // namespace

int main() {
  test_exact_atomic_response();
  test_compositor_may_block_policy_desire();
  test_policy_and_window_staging();
  test_inactive_session_projection();
  test_active_session_invalidation();
  test_session_transition_failures_are_atomic();
}
