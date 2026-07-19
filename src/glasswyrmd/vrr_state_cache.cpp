#include "glasswyrmd/vrr_state_cache.hpp"

#include <algorithm>
#include <utility>

namespace glasswyrm::server {
namespace {

bool valid_policy(const gwipc_vrr_policy_mode value) noexcept {
  return value >= GWIPC_VRR_POLICY_OFF &&
         value <= GWIPC_VRR_POLICY_ALWAYS_ELIGIBLE;
}

bool valid_preference(const gwipc_vrr_window_preference value) noexcept {
  return value >= GWIPC_VRR_PREFERENCE_DEFAULT &&
         value <= GWIPC_VRR_PREFERENCE_PREFER;
}

bool valid_bool(const std::uint8_t value) noexcept { return value <= 1; }

bool valid_capability(const gwipc_output_vrr_capability_upsert& value) {
  return value.struct_size >= sizeof(value) && value.output_id != 0 &&
         valid_bool(value.connector_property_present) &&
         valid_bool(value.hardware_capable) &&
         valid_bool(value.kms_controllable) && valid_bool(value.simulated) &&
         valid_bool(value.range_available) &&
         valid_bool(value.atomic_required) && value.flags == 0 &&
         (value.reason_flags & ~GWIPC_VRR_KNOWN_REASON_MASK) == 0 &&
         (!value.range_available ||
          (value.minimum_refresh_millihertz != 0 &&
           value.minimum_refresh_millihertz <=
               value.maximum_refresh_millihertz));
}

bool valid_policy_record(const gwipc_output_vrr_policy_upsert& value) {
  return value.struct_size >= sizeof(value) && value.output_id != 0 &&
         valid_policy(value.mode) && value.flags == 0;
}

template <class T, class Id>
bool exact_ids(const std::vector<T>& values, const std::set<Id>& expected,
               Id T::*member) {
  std::set<Id> actual;
  for (const auto& value : values)
    if (!actual.insert(value.*member).second) return false;
  return actual == expected;
}

}  // namespace

bool VrrStateCache::replace_inventory(
    std::vector<gwipc_output_vrr_capability_upsert> capabilities,
    std::vector<gwipc_output_vrr_policy_upsert> policies) {
  if (capabilities.empty() || capabilities.size() != policies.size() ||
      expectation_)
    return false;
  std::map<std::uint64_t, ServerVrrOutputState> replacement;
  for (const auto& capability : capabilities) {
    if (!valid_capability(capability) ||
        !replacement.emplace(capability.output_id,
                             ServerVrrOutputState{capability, {}, {}, {}, {}})
             .second)
      return false;
  }
  for (const auto& policy : policies) {
    const auto found = replacement.find(policy.output_id);
    if (!valid_policy_record(policy) || found == replacement.end() ||
        found->second.policy.output_id != 0)
      return false;
    found->second.policy = policy;
  }
  outputs_ = std::move(replacement);
  windows_.clear();
  generation_ = 1;
  return true;
}

bool VrrStateCache::set_policy(const std::uint64_t output_id,
                               const gwipc_vrr_policy_mode mode) noexcept {
  const auto found = outputs_.find(output_id);
  if (found == outputs_.end() || !valid_policy(mode) || expectation_)
    return false;
  found->second.policy.mode = mode;
  found->second.policy_result.reset();
  found->second.compositor_state.reset();
  found->second.timing.reset();
  return true;
}

void VrrStateCache::set_window_preference(
    const std::uint32_t window_id,
    const gwipc_vrr_window_preference preference) {
  if (window_id == 0 || !valid_preference(preference)) return;
  auto [iterator, inserted] = windows_.try_emplace(window_id);
  auto& window = iterator->second;
  if (!inserted && window.preference == preference) return;
  window.preference = preference;
  window.policy_result.reset();
  window.compositor_state.reset();
}

void VrrStateCache::set_window_policy_candidate(
    const std::uint32_t window_id, const bool policy_candidate) {
  if (window_id == 0) return;
  auto [iterator, inserted] = windows_.try_emplace(window_id);
  auto& window = iterator->second;
  if (!inserted && window.policy_candidate == policy_candidate) return;
  window.policy_candidate = policy_candidate;
  window.policy_result.reset();
  window.compositor_state.reset();
}

void VrrStateCache::erase_window(const std::uint32_t window_id) noexcept {
  windows_.erase(window_id);
}

bool VrrStateCache::stage_policy_result(
    const std::uint64_t generation,
    const std::vector<gwipc_policy_output_vrr_state>& output_results,
    const std::vector<gwipc_policy_window_vrr_state>& window_results) {
  std::set<std::uint32_t> expected_windows;
  for (const auto& [window_id, window] : windows_)
    if (window.policy_candidate) expected_windows.insert(window_id);
  if (generation == 0 || output_results.size() != outputs_.size() ||
      window_results.size() != expected_windows.size())
    return false;
  std::set<std::uint64_t> output_ids;
  std::map<std::uint32_t, std::uint64_t> selected_windows;
  for (const auto& result : output_results) {
    const auto found = outputs_.find(result.output_id);
    if (result.struct_size < sizeof(result) || found == outputs_.end() ||
        !output_ids.insert(result.output_id).second ||
        !valid_policy(result.mode) || result.mode != found->second.policy.mode ||
        !valid_bool(result.desired_enabled) ||
        !valid_bool(result.candidate_required) || result.flags != 0 ||
        (result.reason_flags & ~GWIPC_VRR_KNOWN_REASON_MASK) != 0 ||
        (result.selected_window_id != 0 &&
         !selected_windows.emplace(result.selected_window_id,
                                   result.output_id).second))
      return false;
  }
  std::set<std::uint32_t> window_ids;
  for (const auto& result : window_results) {
    const auto found = windows_.find(result.window_id);
    if (result.struct_size < sizeof(result) || found == windows_.end() ||
        !found->second.policy_candidate ||
        !window_ids.insert(result.window_id).second ||
        !outputs_.contains(result.output_id) ||
        !valid_preference(result.preference) ||
        result.preference != found->second.preference ||
        !valid_bool(result.selected) || !valid_bool(result.eligible) ||
        !valid_bool(result.focused) || !valid_bool(result.fullscreen) ||
        !valid_bool(result.borderless_fullscreen) ||
        !valid_bool(result.exclusive_output_membership) || result.flags != 0 ||
        (result.reason_flags & ~GWIPC_VRR_KNOWN_REASON_MASK) != 0 ||
        (result.selected &&
         (!selected_windows.contains(result.window_id) ||
          selected_windows.at(result.window_id) != result.output_id)))
      return false;
  }
  if (window_ids != expected_windows) return false;
  for (const auto& [selected, output_id] : selected_windows) {
    static_cast<void>(output_id);
    const auto found = std::ranges::find_if(
        window_results, [selected](const auto& value) {
          return value.window_id == selected && value.selected != 0;
        });
    if (found == window_results.end()) return false;
  }
  for (const auto& result : output_results)
    outputs_.at(result.output_id).policy_result = result;
  for (auto& [window_id, window] : windows_) {
    static_cast<void>(window_id);
    window.policy_result.reset();
  }
  for (const auto& result : window_results)
    windows_.at(result.window_id).policy_result = result;
  generation_ = generation;
  return true;
}

bool VrrStateCache::stage_surface_states(
    const std::vector<gwipc_surface_vrr_state>& states) {
  if (states.size() != windows_.size()) return false;
  std::set<std::uint32_t> windows;
  std::set<std::uint64_t> surfaces;
  for (const auto& state : states) {
    const auto found = windows_.find(state.window_id);
    const auto policy = found == windows_.end()
                            ? nullptr
                            : found->second.policy_result
                                  ? &*found->second.policy_result
                                  : nullptr;
    if (state.struct_size < sizeof(state) || state.surface_id == 0 ||
        found == windows_.end() ||
        (found->second.policy_candidate && !policy) ||
        !outputs_.contains(state.output_id) ||
        !windows.insert(state.window_id).second ||
        !surfaces.insert(state.surface_id).second ||
        !valid_preference(state.preference) ||
        state.preference != found->second.preference ||
        !valid_bool(state.policy_selected) ||
        !valid_bool(state.policy_eligible) || !valid_bool(state.focused) ||
        !valid_bool(state.fullscreen) ||
        !valid_bool(state.borderless_fullscreen) ||
        !valid_bool(state.exclusive_output_membership) || state.flags != 0 ||
        state.policy_generation != generation_ ||
        (state.reason_flags & ~GWIPC_VRR_KNOWN_REASON_MASK) != 0 ||
        (found->second.policy_candidate &&
         (state.output_id != policy->output_id ||
          state.policy_selected != policy->selected ||
          state.policy_eligible != policy->eligible ||
          state.focused != policy->focused ||
          state.fullscreen != policy->fullscreen ||
          state.borderless_fullscreen != policy->borderless_fullscreen ||
          state.exclusive_output_membership !=
              policy->exclusive_output_membership ||
          state.reason_flags != policy->reason_flags)) ||
        (!found->second.policy_candidate &&
         (policy || state.policy_selected || state.policy_eligible ||
          (state.reason_flags & GWIPC_VRR_REASON_WINDOW_UNMANAGED) == 0)))
      return false;
  }
  for (const auto& state : states)
    windows_.at(state.window_id).compositor_state = state;
  return true;
}

bool VrrStateCache::seed_compositor_state(
    const std::vector<gwipc_output_vrr_state_upsert>& states,
    const std::vector<gwipc_presentation_timing>& timings) {
  if (expectation_ || states.size() != outputs_.size()) return false;
  std::set<std::uint64_t> state_ids;
  for (const auto& state : states) {
    const auto output = outputs_.find(state.output_id);
    if (output == outputs_.end() || !state_ids.insert(state.output_id).second ||
        state.struct_size < sizeof(state) ||
        state.requested_mode != output->second.policy.mode ||
        state.decision < GWIPC_VRR_DECISION_DISABLED ||
        state.decision > GWIPC_VRR_DECISION_REJECTED ||
        !valid_bool(state.desired_enabled) ||
        !valid_bool(state.effective_enabled) ||
        !valid_bool(state.property_readback_valid) ||
        !valid_bool(state.session_active) || state.state_generation == 0 ||
        state.flags != 0 ||
        (state.reason_flags & ~GWIPC_VRR_KNOWN_REASON_MASK) != 0)
      return false;
  }
  std::set<std::uint64_t> timing_ids;
  for (const auto& timing : timings)
    if (!outputs_.contains(timing.output_id) ||
        !timing_ids.insert(timing.output_id).second ||
        timing.struct_size < sizeof(timing) || timing.commit_id == 0 ||
        timing.presented_generation == 0 ||
        !valid_bool(timing.effective_vrr_enabled) ||
        !valid_bool(timing.timestamp_available) ||
        (timing.flags & ~GWIPC_PRESENTATION_TIMING_SIMULATED) != 0)
      return false;
  for (const auto& timing : timings) {
    const auto state = std::ranges::find_if(states, [&](const auto& value) {
      return value.output_id == timing.output_id;
    });
    if (state == states.end() ||
        state->effective_enabled != timing.effective_vrr_enabled ||
        state->last_commit_id != timing.commit_id ||
        state->last_presented_generation != timing.presented_generation ||
        state->last_interval_nanoseconds != timing.interval_nanoseconds)
      return false;
  }
  for (const auto& state : states)
    outputs_.at(state.output_id).compositor_state = state;
  for (const auto& timing : timings)
    outputs_.at(timing.output_id).timing = timing;
  return true;
}

bool VrrStateCache::expect_response(VrrResponseExpectation expectation) {
  if (expectation_ || expectation.commit_id == 0 ||
      expectation.presented_generation == 0 || expectation.output_ids.empty())
    return false;
  for (const auto output : expectation.output_ids)
    if (!outputs_.contains(output)) return false;
  expectation_ = std::move(expectation);
  return true;
}

VrrResponseStatus VrrStateCache::preflight(
    const VrrResponseBatch& batch) const noexcept {
  if (!expectation_) return VrrResponseStatus::NoExpectation;
  const auto& expected = *expectation_;
  if (!batch.acknowledgement ||
      batch.acknowledgement->struct_size < sizeof(gwipc_frame_acknowledged) ||
      batch.acknowledgement->commit_id != expected.commit_id ||
      batch.acknowledgement->presented_generation !=
          expected.presented_generation ||
      batch.acknowledgement->result != GWIPC_FRAME_ACCEPTED)
    return VrrResponseStatus::InvalidAcknowledgement;
  if (batch.output_states.size() != expected.output_ids.size())
    return VrrResponseStatus::OutputCountMismatch;
  std::set<std::uint64_t> state_ids;
  for (const auto& state : batch.output_states) {
    if (!state_ids.insert(state.output_id).second)
      return VrrResponseStatus::DuplicateOutput;
    const auto found = outputs_.find(state.output_id);
    if (found == outputs_.end() || !expected.output_ids.contains(state.output_id))
      return VrrResponseStatus::UnknownOutput;
    if (state.struct_size < sizeof(state) ||
        state.requested_mode != found->second.policy.mode ||
        state.decision < GWIPC_VRR_DECISION_DISABLED ||
        state.decision > GWIPC_VRR_DECISION_REJECTED ||
        !valid_bool(state.desired_enabled) ||
        !valid_bool(state.effective_enabled) ||
        !valid_bool(state.property_readback_valid) ||
        !valid_bool(state.session_active) || state.state_generation == 0 ||
        state.last_commit_id != expected.commit_id ||
        state.last_presented_generation != expected.presented_generation ||
        state.flags != 0 ||
        (state.reason_flags & ~GWIPC_VRR_KNOWN_REASON_MASK) != 0 ||
        (found->second.policy_result &&
         (state.desired_enabled !=
              found->second.policy_result->desired_enabled ||
          state.candidate_window_id !=
              found->second.policy_result->selected_window_id ||
          state.candidate_surface_id !=
              (state.candidate_window_id == 0
                   ? UINT64_C(0)
                   : (UINT64_C(1) << 32U) | state.candidate_window_id))))
      return VrrResponseStatus::InvalidOutputState;
  }
  if (state_ids != expected.output_ids)
    return VrrResponseStatus::OutputCountMismatch;
  if (batch.timings.size() != expected.output_ids.size())
    return VrrResponseStatus::TimingCountMismatch;
  std::set<std::uint64_t> timing_ids;
  for (const auto& timing : batch.timings) {
    if (!timing_ids.insert(timing.output_id).second)
      return VrrResponseStatus::DuplicateTiming;
    if (!expected.output_ids.contains(timing.output_id) ||
        timing.struct_size < sizeof(timing) ||
        timing.commit_id != expected.commit_id ||
        timing.presented_generation != expected.presented_generation ||
        !valid_bool(timing.effective_vrr_enabled) ||
        !valid_bool(timing.timestamp_available) ||
        (timing.flags & ~GWIPC_PRESENTATION_TIMING_SIMULATED) != 0)
      return VrrResponseStatus::InvalidTiming;
  }
  if (timing_ids != expected.output_ids)
    return VrrResponseStatus::TimingCountMismatch;
  for (const auto& timing : batch.timings) {
    const auto state = std::ranges::find_if(
        batch.output_states, [&](const auto& value) {
          return value.output_id == timing.output_id;
        });
    if (state == batch.output_states.end() ||
        timing.effective_vrr_enabled != state->effective_enabled ||
        timing.interval_nanoseconds != state->last_interval_nanoseconds)
      return VrrResponseStatus::InvalidTiming;
  }
  const std::set<std::uint64_t> releases(batch.released_buffer_ids.begin(),
                                         batch.released_buffer_ids.end());
  if (releases.size() != batch.released_buffer_ids.size() ||
      releases != expected.release_buffer_ids)
    return VrrResponseStatus::ReleaseMismatch;
  return VrrResponseStatus::Accepted;
}

VrrResponseStatus VrrStateCache::promote(const VrrResponseBatch& batch) {
  const auto status = preflight(batch);
  if (status != VrrResponseStatus::Accepted) return status;
  for (const auto& state : batch.output_states)
    outputs_.at(state.output_id).compositor_state = state;
  for (const auto& timing : batch.timings)
    outputs_.at(timing.output_id).timing = timing;
  expectation_.reset();
  return VrrResponseStatus::Accepted;
}

const char* vrr_response_status_name(const VrrResponseStatus status) noexcept {
  switch (status) {
    case VrrResponseStatus::Accepted: return "accepted";
    case VrrResponseStatus::NoExpectation: return "no-expectation";
    case VrrResponseStatus::InvalidAcknowledgement:
      return "invalid-acknowledgement";
    case VrrResponseStatus::OutputCountMismatch:
      return "output-count-mismatch";
    case VrrResponseStatus::DuplicateOutput: return "duplicate-output";
    case VrrResponseStatus::UnknownOutput: return "unknown-output";
    case VrrResponseStatus::InvalidOutputState: return "invalid-output-state";
    case VrrResponseStatus::TimingCountMismatch:
      return "timing-count-mismatch";
    case VrrResponseStatus::DuplicateTiming: return "duplicate-timing";
    case VrrResponseStatus::InvalidTiming: return "invalid-timing";
    case VrrResponseStatus::ReleaseMismatch: return "release-mismatch";
  }
  return "unknown";
}

}  // namespace glasswyrm::server
