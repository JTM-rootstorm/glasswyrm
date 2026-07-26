#include "glasswyrmd/vrr_state_cache.hpp"

#include <limits>

namespace glasswyrm::server {
namespace {

constexpr std::uint64_t kSessionInactiveReasons =
    GWIPC_VRR_REASON_SESSION_INACTIVE | GWIPC_VRR_REASON_VT_SUSPENDED |
    GWIPC_VRR_REASON_TIMING_UNAVAILABLE;

bool inactive_state_differs(
    const gwipc_output_vrr_state_upsert& state,
    const gwipc_policy_output_vrr_state& policy,
    const std::uint64_t generation) noexcept {
  return state.decision != GWIPC_VRR_DECISION_DISABLED ||
         state.desired_enabled != 0 ||
         state.effective_enabled != 0 || state.session_active != 0 ||
         state.candidate_window_id != policy.selected_window_id ||
         (state.reason_flags & kSessionInactiveReasons) !=
             kSessionInactiveReasons ||
         state.state_generation != generation;
}

bool coherent_cached_output(
    const std::uint64_t output_id,
    const ServerVrrOutputState& output) noexcept {
  if (!output.compositor_state || !output.policy_result) return false;
  const auto& state = *output.compositor_state;
  const auto& policy = *output.policy_result;
  if (state.struct_size < sizeof(state) || state.output_id != output_id ||
      state.requested_mode != output.policy.mode ||
      policy.output_id != output_id || policy.mode != output.policy.mode ||
      (policy.desired_enabled != 0 && policy.desired_enabled != 1) ||
      (policy.reason_flags & ~GWIPC_VRR_KNOWN_REASON_MASK) != 0) {
    return false;
  }
  return !output.timing ||
         (output.timing->output_id == output_id &&
          output.timing->commit_id == state.last_commit_id &&
          output.timing->presented_generation ==
              state.last_presented_generation &&
          output.timing->effective_vrr_enabled == state.effective_enabled &&
          output.timing->interval_nanoseconds ==
              state.last_interval_nanoseconds);
}

}  // namespace

VrrSessionStateStatus VrrStateCache::apply_session_state(
    const gwipc_session_state session_state) {
  if (session_state != GWIPC_SESSION_INACTIVE &&
      session_state != GWIPC_SESSION_ACTIVE) {
    return VrrSessionStateStatus::InvalidState;
  }
  if (session_state == GWIPC_SESSION_ACTIVE) {
    if (expectation_)
      invalidate_after_response_ = true;
    else
      invalidate_compositor_state();
    return VrrSessionStateStatus::Applied;
  }

  for (const auto& [output_id, output] : outputs_) {
    if (!output.compositor_state) {
      return VrrSessionStateStatus::OutputStateMissing;
    }
    if (!coherent_cached_output(output_id, output)) {
      return VrrSessionStateStatus::OutputStateIncoherent;
    }
    const auto& state = *output.compositor_state;
    if (inactive_state_differs(
            state, *output.policy_result, generation_) &&
        state.transition_serial == std::numeric_limits<std::uint64_t>::max()) {
      return VrrSessionStateStatus::TransitionSerialExhausted;
    }
  }

  for (auto& [output_id, output] : outputs_) {
    static_cast<void>(output_id);
    auto& state = *output.compositor_state;
    const auto& policy = *output.policy_result;
    const bool changed =
        inactive_state_differs(state, policy, generation_);
    state.decision = GWIPC_VRR_DECISION_DISABLED;
    state.desired_enabled = 0;
    state.effective_enabled = 0;
    state.session_active = 0;
    state.candidate_window_id = policy.selected_window_id;
    state.candidate_surface_id =
        policy.selected_window_id == 0
            ? UINT64_C(0)
            : (UINT64_C(1) << 32U) | policy.selected_window_id;
    state.reason_flags |= policy.reason_flags | kSessionInactiveReasons;
    state.state_generation = generation_;
    if (changed) ++state.transition_serial;
    if (output.timing) output.timing->effective_vrr_enabled = 0;
  }
  return VrrSessionStateStatus::Applied;
}

}  // namespace glasswyrm::server
