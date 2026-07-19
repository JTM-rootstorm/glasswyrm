#include "output/vrr/decision.hpp"

namespace glasswyrm::output::vrr {
namespace {

void add_reason(ReasonMask &mask, const Reason reason) noexcept {
  mask |= reason_bit(reason);
}

bool candidate_required(const PolicyMode mode) noexcept {
  return mode == PolicyMode::Fullscreen || mode == PolicyMode::Focused ||
         mode == PolicyMode::AppRequested;
}

void evaluate_output(const DecisionInput &input, ReasonMask &blocking,
                     ReasonMask &reasons) noexcept {
  const auto block = [&](const Reason reason) {
    add_reason(blocking, reason);
    add_reason(reasons, reason);
  };
  if (!input.output.enabled)
    block(Reason::OutputDisabled);
  if (!input.output.connected)
    block(Reason::OutputNotConnected);

  if (input.output.simulated) {
    add_reason(reasons, Reason::SimulatedHeadless);
  } else {
    if (!input.output.drm)
      block(Reason::OutputNotDrm);
    if (!input.output.hardware_capable)
      block(Reason::OutputNotVrrCapable);
    if (!input.output.atomic_kms_available)
      block(Reason::AtomicKmsUnavailable);
    if (!input.output.vrr_property_present)
      block(Reason::VrrPropertyMissing);
    if (!input.output.atomic_test_passed)
      block(Reason::VrrAtomicTestFailed);
  }
  if (!input.output.kms_controllable)
    block(Reason::PresenterRejected);
  if (!input.display.session_active)
    block(Reason::SessionInactive);
  if (input.display.vt_suspended)
    block(Reason::VtSuspended);
  if (input.display.output_configuration_busy)
    block(Reason::OutputConfigurationBusy);
  if (!input.presenter.accepted)
    block(Reason::PresenterRejected);
  if (!input.presenter.property_readback_matches)
    block(Reason::PropertyReadbackMismatch);
  if (!input.presenter.timing_available)
    add_reason(reasons, Reason::TimingUnavailable);
  if (!input.output.simulated && !input.presenter.hardware_behavior_confirmed)
    add_reason(reasons, Reason::HardwareBehaviorUnconfirmed);
}

void evaluate_window(const DecisionInput &input, ReasonMask &blocking,
                     ReasonMask &reasons) noexcept {
  const auto block = [&](const Reason reason) {
    add_reason(blocking, reason);
    add_reason(reasons, reason);
  };
  const auto &candidate = input.candidate;
  if (candidate.window_id == 0 || !candidate.window_present)
    block(Reason::WindowMissing);
  if (!candidate.visible)
    block(Reason::WindowHidden);
  if (!candidate.managed)
    block(Reason::WindowUnmanaged);
  if (!candidate.focused)
    block(Reason::WindowUnfocused);
  if (input.mode == PolicyMode::Fullscreen && !candidate.fullscreen &&
      !candidate.borderless_fullscreen) {
    block(Reason::WindowNotFullscreen);
    block(Reason::WindowNotBorderlessFullscreen);
  }
  if (!candidate.exclusive_output_membership)
    block(Reason::WindowSpansOutputs);
  if (!valid_window_preference(candidate.preference) ||
      candidate.preference == WindowPreference::Disable)
    block(Reason::WindowPreferenceDisabled);
  if (input.mode == PolicyMode::AppRequested &&
      candidate.preference != WindowPreference::Prefer)
    block(Reason::WindowDidNotRequest);
}

void evaluate_surface(const CandidateFacts &candidate, ReasonMask &blocking,
                      ReasonMask &reasons) noexcept {
  const auto block = [&](const Reason reason) {
    add_reason(blocking, reason);
    add_reason(reasons, reason);
  };
  if (candidate.surface_id == 0 || !candidate.surface_present)
    block(Reason::SurfaceMissing);
  if (candidate.surface_metadata_only)
    block(Reason::SurfaceMetadataOnly);
  if (!candidate.surface_visible)
    block(Reason::SurfaceNotVisible);
  if (!candidate.surface_opaque)
    block(Reason::SurfaceNotOpaque);
  if (!candidate.surface_on_output)
    block(Reason::SurfaceOnWrongOutput);
  if (!candidate.surface_membership_valid)
    block(Reason::SurfaceMembershipInvalid);
}

Decision blocked_decision(const Reason primary) noexcept {
  switch (primary) {
  case Reason::OutputNotDrm:
  case Reason::OutputNotVrrCapable:
  case Reason::AtomicKmsUnavailable:
  case Reason::VrrPropertyMissing:
  case Reason::VrrAtomicTestFailed:
    return Decision::Unsupported;
  case Reason::PresenterRejected:
  case Reason::PropertyReadbackMismatch:
    return Decision::Rejected;
  default:
    return Decision::Disabled;
  }
}

} // namespace

DecisionResult evaluate(const DecisionInput &input) noexcept {
  DecisionResult result;
  result.candidate_required = candidate_required(input.mode);
  ReasonMask blocking{};
  evaluate_output(input, blocking, result.reasons);

  if (!valid_policy_mode(input.mode)) {
    add_reason(blocking, Reason::PolicyOff);
    add_reason(result.reasons, Reason::PolicyOff);
  } else if (input.mode == PolicyMode::Off) {
    add_reason(blocking, Reason::PolicyOff);
    add_reason(result.reasons, Reason::PolicyOff);
  } else if (input.mode == PolicyMode::AlwaysEligible) {
    add_reason(result.reasons, Reason::ManualAlwaysEligible);
    if (input.candidate.selected) {
      result.candidate_window_id = input.candidate.window_id;
      result.candidate_surface_id = input.candidate.surface_id;
    }
  } else if (!input.candidate.selected) {
    add_reason(blocking, Reason::NoCandidate);
    add_reason(result.reasons, Reason::NoCandidate);
  } else {
    result.candidate_window_id = input.candidate.window_id;
    result.candidate_surface_id = input.candidate.surface_id;
    evaluate_window(input, blocking, result.reasons);
    evaluate_surface(input.candidate, blocking, result.reasons);
  }

  result.primary = primary_reason(result.reasons);
  if (blocking == 0) {
    result.desired_enabled = true;
    result.decision = Decision::Enabled;
  } else {
    const auto blocker = primary_reason(blocking);
    result.decision = blocker ? blocked_decision(*blocker) : Decision::Disabled;
  }
  return result;
}

} // namespace glasswyrm::output::vrr
