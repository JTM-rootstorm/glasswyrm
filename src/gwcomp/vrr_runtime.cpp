#include "gwcomp/vrr_runtime.hpp"

#include <algorithm>
#include <limits>

namespace gw::compositor {
namespace {

using glasswyrm::output::VrrPresentationCapability;
using glasswyrm::output::VrrPresentationRequest;
using glasswyrm::output::vrr::CandidateFacts;
using glasswyrm::output::vrr::Decision;
using glasswyrm::output::vrr::DecisionInput;
using glasswyrm::output::vrr::PolicyMode;
using glasswyrm::output::vrr::Reason;
using glasswyrm::output::vrr::WindowPreference;

std::uint64_t refresh_interval_nanoseconds(
    const std::uint32_t refresh_millihertz) noexcept {
  constexpr std::uint64_t nanoseconds_per_millihertz = UINT64_C(1000000000000);
  if (refresh_millihertz == 0) return 0;
  return (nanoseconds_per_millihertz + refresh_millihertz / 2U) /
         refresh_millihertz;
}

PolicyMode mode(const gwipc_vrr_policy_mode value) noexcept {
  return static_cast<PolicyMode>(value);
}

WindowPreference preference(
    const gwipc_vrr_window_preference value) noexcept {
  return static_cast<WindowPreference>(value);
}

gwipc_vrr_policy_mode mode(const PolicyMode value) noexcept {
  return static_cast<gwipc_vrr_policy_mode>(value);
}

gwipc_vrr_decision decision(const Decision value) noexcept {
  return static_cast<gwipc_vrr_decision>(value);
}

bool changed(const gwipc_output_vrr_state_upsert& previous,
             const VrrPresentationRequest& current) noexcept {
  return previous.requested_mode != mode(current.requested_mode) ||
         previous.decision != decision(current.decision) ||
         previous.desired_enabled != current.desired_enabled ||
         previous.candidate_window_id != current.candidate_window_id ||
         previous.candidate_surface_id != current.candidate_surface_id ||
         previous.reason_flags != current.reason_flags;
}

CandidateFacts candidate_for(const Scene& scene,
                             const std::uint64_t output_id) {
  CandidateFacts facts;
  for (const auto& [surface_id, state] : scene.vrr.surfaces) {
    if (state.output_id != output_id || !state.policy_selected)
      continue;
    facts.selected = true;
    facts.window_id = state.window_id;
    facts.surface_id = surface_id;
    facts.preference = preference(state.preference);
    facts.focused = state.focused != 0;
    facts.fullscreen = state.fullscreen != 0;
    facts.borderless_fullscreen = state.borderless_fullscreen != 0;
    facts.exclusive_output_membership =
        state.exclusive_output_membership != 0;
    const auto surface = scene.surfaces.find(surface_id);
    facts.surface_present = surface != scene.surfaces.end();
    if (!facts.surface_present)
      return facts;
    facts.window_present = surface->second.x11_window_id == state.window_id;
    facts.visible = surface->second.visible != 0;
    const auto policy = scene.surface_policies.find(surface_id);
    facts.managed = policy != scene.surface_policies.end() &&
                    policy->second.x11_window_id == state.window_id;
    facts.surface_metadata_only =
        surface->second.presentation_flags ==
        GWIPC_SURFACE_PRESENTATION_METADATA_ONLY;
    facts.surface_visible = surface->second.visible != 0;
    facts.surface_opaque = surface->second.opacity == GWIPC_OPACITY_ONE;
    facts.surface_on_output = state.output_id == output_id;
    const auto membership = scene.surface_outputs.find(surface_id);
    facts.surface_membership_valid =
        membership != scene.surface_outputs.end() &&
        membership->second.primary_output_id == output_id &&
        membership->second.output_ids.size() == 1 &&
        membership->second.output_ids.front() == output_id;
    return facts;
  }
  return facts;
}

} // namespace

std::optional<PreparedVrrFrame> VrrRuntime::prepare(
    const Scene& scene,
    const glasswyrm::output::PresentationBackend& presenter,
    const CommittedVrrState& committed, std::string& error) {
  PreparedVrrFrame prepared;
  for (const auto& [output_id, output] : scene.outputs) {
    const auto policy = scene.vrr.output_policies.find(output_id);
    if (policy == scene.vrr.output_policies.end()) {
      error = "VRR runtime is missing an output policy";
      return std::nullopt;
    }
    // Disabled outputs remain part of the complete output-policy snapshot, but
    // they do not have a framebuffer in this presentation transaction.  The
    // M14 response is deliberately exact to the presented frame set.
    if (output.enabled == 0)
      continue;
    auto capability = presenter.vrr_capability(output_id);
    if (!capability) {
      error = "presentation backend did not report VRR capability";
      return std::nullopt;
    }
    capability->output_enabled = output.enabled != 0;
    const DecisionInput input{
        mode(policy->second.mode),
        {output.enabled != 0, capability->connected, capability->drm,
         capability->hardware_capable, capability->atomic_kms_available,
         capability->connector_property_present,
         capability->atomic_test_passed, capability->kms_controllable,
         capability->simulated},
        {capability->session_active, capability->suspended, false},
        {capability->kms_controllable, true, capability->timing_available,
         false},
        candidate_for(scene, output_id)};
    const auto result = glasswyrm::output::vrr::evaluate(input);
    VrrPresentationRequest request{
        true,
        input.mode,
        result.decision,
        result.desired_enabled,
        result.candidate_window_id,
        result.candidate_surface_id,
        result.reasons | capability->reason_flags,
        scene.vrr.policy_generation != 0 ? scene.vrr.policy_generation
                                         : scene.configuration_generation,
        1,
        refresh_interval_nanoseconds(output.refresh_millihertz)};
    const auto old = committed.outputs().find(output_id);
    if (old != committed.outputs().end()) {
      request.transition_serial = old->second.transition_serial;
      if (changed(old->second, request)) {
        if (request.transition_serial ==
            std::numeric_limits<std::uint64_t>::max()) {
          error = "VRR transition serial is exhausted";
          return std::nullopt;
        }
        ++request.transition_serial;
      }
    }
    prepared.requests.emplace(output_id, request);
    prepared.capabilities.emplace(output_id, *capability);
  }
  error.clear();
  return prepared;
}

std::optional<CompletedVrrFrame> VrrRuntime::complete(
    const PreparedVrrFrame& prepared,
    const glasswyrm::output::VrrPresentationFeedbackMap& feedback,
    const std::uint64_t commit_id,
    const std::uint64_t presented_generation, std::string& error) {
  if (commit_id == 0 || presented_generation == 0 ||
      prepared.requests.empty() || feedback.size() != prepared.requests.size()) {
    error = "presentation backend returned an incomplete VRR result";
    return std::nullopt;
  }
  CompletedVrrFrame completed;
  for (const auto& [output_id, request] : prepared.requests) {
    const auto actual = feedback.find(output_id);
    const auto capability = prepared.capabilities.find(output_id);
    if (actual == feedback.end() || capability == prepared.capabilities.end() ||
        actual->second.output_id != output_id ||
        (actual->second.flags &
         ~glasswyrm::output::kVrrPresentationFeedbackSimulated) != 0 ||
        (!actual->second.timestamp_available &&
         (actual->second.kernel_timestamp_nanoseconds != 0 ||
          actual->second.interval_nanoseconds != 0)) ||
        (actual->second.timestamp_available &&
         actual->second.kernel_timestamp_nanoseconds == 0)) {
      error = "presentation backend returned VRR state for the wrong output";
      return std::nullopt;
    }
    auto effective_decision = request.decision;
    auto reasons = request.reason_flags;
    const bool simulated = capability->second.simulated;
    if (request.desired_enabled && !actual->second.effective_enabled) {
      effective_decision = Decision::Rejected;
      reasons |= glasswyrm::output::vrr::reason_bit(Reason::PresenterRejected);
    }
    if (actual->second.effective_enabled &&
        (!request.desired_enabled ||
         (!actual->second.property_readback_valid && !simulated))) {
      error = "presentation backend reported impossible effective VRR state";
      return std::nullopt;
    }
    gwipc_output_vrr_state_upsert state{};
    state.struct_size = sizeof(state);
    state.output_id = output_id;
    state.requested_mode = mode(request.requested_mode);
    state.decision = decision(effective_decision);
    state.desired_enabled = request.desired_enabled;
    state.effective_enabled = actual->second.effective_enabled;
    state.property_readback_valid = actual->second.property_readback_valid;
    state.session_active = actual->second.session_active;
    state.candidate_window_id = request.candidate_window_id;
    state.candidate_surface_id = request.candidate_surface_id;
    state.reason_flags = reasons;
    state.state_generation = request.state_generation;
    state.transition_serial = request.transition_serial;
    state.last_commit_id = commit_id;
    state.last_presented_generation = presented_generation;
    state.last_flip_sequence = actual->second.flip_sequence;
    state.last_flip_timestamp_nanoseconds =
        actual->second.kernel_timestamp_nanoseconds;
    state.last_interval_nanoseconds = actual->second.interval_nanoseconds;
    completed.states.emplace(output_id, state);

    gwipc_presentation_timing timing{};
    timing.struct_size = sizeof(timing);
    timing.output_id = output_id;
    timing.commit_id = commit_id;
    timing.presented_generation = presented_generation;
    timing.flip_sequence = actual->second.flip_sequence;
    timing.flags = actual->second.flags;
    if (simulated)
      timing.flags |= GWIPC_PRESENTATION_TIMING_SIMULATED;
    timing.kernel_timestamp_nanoseconds =
        actual->second.kernel_timestamp_nanoseconds;
    timing.interval_nanoseconds = actual->second.interval_nanoseconds;
    timing.effective_vrr_enabled = actual->second.effective_enabled;
    timing.timestamp_available = actual->second.timestamp_available;
    completed.timings.emplace(output_id, timing);
  }
  error.clear();
  return completed;
}

} // namespace gw::compositor
