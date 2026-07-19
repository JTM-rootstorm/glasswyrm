#include "glasswyrmd/vrr_state_cache.hpp"
#include "tests/helpers/test_support.hpp"

using glasswyrm::server::VrrResponseBatch;
using glasswyrm::server::VrrResponseExpectation;
using glasswyrm::server::VrrResponseStatus;
using glasswyrm::server::VrrStateCache;
using gw::test::require;

namespace {

gwipc_output_vrr_capability_upsert capability() {
  gwipc_output_vrr_capability_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = 7;
  value.connector_property_present = 1;
  value.hardware_capable = 1;
  value.kms_controllable = 1;
  value.range_available = 1;
  value.atomic_required = 1;
  value.minimum_refresh_millihertz = 40'000;
  value.maximum_refresh_millihertz = 144'000;
  return value;
}

gwipc_output_vrr_policy_upsert policy() {
  gwipc_output_vrr_policy_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = 7;
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

}  // namespace

int main() {
  test_exact_atomic_response();
  test_policy_and_window_staging();
}
