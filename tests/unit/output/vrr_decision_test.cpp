#include "output/vrr/decision.hpp"

#include "helpers/test_support.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace {

using namespace glasswyrm::output::vrr;
using gw::test::require;

DecisionInput eligible(const PolicyMode mode = PolicyMode::Focused) {
  DecisionInput input;
  input.mode = mode;
  input.output = {true, true, true, true, true, true, true, true, false};
  input.display = {true, false, false};
  input.presenter = {true, true, true, true};
  input.candidate = {true,
                     41,
                     51,
                     true,
                     true,
                     true,
                     true,
                     true,
                     false,
                     true,
                     WindowPreference::Default,
                     true,
                     false,
                     true,
                     true,
                     true,
                     true};
  return input;
}

void require_blocked(const DecisionInput &input, const Reason reason,
                     const Decision decision, const char *message) {
  const auto result = evaluate(input);
  require(!result.desired_enabled && result.decision == decision &&
              has_reason(result.reasons, reason),
          message);
}

void test_modes_and_candidate_semantics() {
  auto input = eligible(PolicyMode::Off);
  auto result = evaluate(input);
  require(!result.desired_enabled && result.decision == Decision::Disabled &&
              result.primary == Reason::PolicyOff &&
              result.candidate_window_id == 0 &&
              result.candidate_surface_id == 0,
          "Off disables VRR and publishes no candidate");

  input = eligible(PolicyMode::Fullscreen);
  result = evaluate(input);
  require(result.desired_enabled && result.decision == Decision::Enabled &&
              result.candidate_required && result.candidate_window_id == 41 &&
              result.candidate_surface_id == 51,
          "Fullscreen accepts an eligible fullscreen candidate");

  input.candidate.fullscreen = false;
  input.candidate.borderless_fullscreen = true;
  result = evaluate(input);
  require(result.desired_enabled && result.decision == Decision::Enabled,
          "Fullscreen accepts a compositor-valid borderless candidate");

  input = eligible(PolicyMode::Focused);
  input.candidate.fullscreen = false;
  result = evaluate(input);
  require(result.desired_enabled && result.decision == Decision::Enabled,
          "Focused accepts an eligible windowed candidate");

  input = eligible(PolicyMode::AppRequested);
  input.candidate.preference = WindowPreference::Default;
  require_blocked(input, Reason::WindowDidNotRequest, Decision::Disabled,
                  "AppRequested rejects Default preference");
  input.candidate.preference = WindowPreference::Allow;
  require_blocked(input, Reason::WindowDidNotRequest, Decision::Disabled,
                  "AppRequested rejects Allow preference");
  input.candidate.preference = WindowPreference::Prefer;
  result = evaluate(input);
  require(result.desired_enabled && result.decision == Decision::Enabled,
          "AppRequested accepts Prefer preference");

  input = eligible(PolicyMode::AlwaysEligible);
  input.candidate.selected = false;
  input.candidate.preference = WindowPreference::Disable;
  result = evaluate(input);
  require(result.desired_enabled && result.decision == Decision::Enabled &&
              !result.candidate_required &&
              has_reason(result.reasons, Reason::ManualAlwaysEligible) &&
              !has_reason(result.reasons, Reason::NoCandidate) &&
              !has_reason(result.reasons, Reason::WindowPreferenceDisabled),
          "AlwaysEligible ignores candidate absence and application opt-out");
}

void test_output_session_and_presenter_reasons() {
  auto input = eligible();
  input.output.enabled = false;
  require_blocked(input, Reason::OutputDisabled, Decision::Disabled,
                  "disabled output is reported");
  input = eligible();
  input.output.connected = false;
  require_blocked(input, Reason::OutputNotConnected, Decision::Disabled,
                  "disconnected output is reported");
  input = eligible();
  input.output.drm = false;
  require_blocked(input, Reason::OutputNotDrm, Decision::Unsupported,
                  "legacy non-DRM output is unsupported");
  input = eligible();
  input.output.hardware_capable = false;
  require_blocked(input, Reason::OutputNotVrrCapable, Decision::Unsupported,
                  "incapable physical output is unsupported");
  input = eligible();
  input.output.atomic_kms_available = false;
  require_blocked(input, Reason::AtomicKmsUnavailable, Decision::Unsupported,
                  "legacy KMS is unsupported");
  input = eligible();
  input.output.vrr_property_present = false;
  require_blocked(input, Reason::VrrPropertyMissing, Decision::Unsupported,
                  "missing VRR property is unsupported");
  input = eligible();
  input.output.atomic_test_passed = false;
  require_blocked(input, Reason::VrrAtomicTestFailed, Decision::Unsupported,
                  "failed atomic test is unsupported");
  input = eligible();
  input.display.session_active = false;
  require_blocked(input, Reason::SessionInactive, Decision::Disabled,
                  "inactive session disables VRR");
  input = eligible();
  input.display.vt_suspended = true;
  require_blocked(input, Reason::VtSuspended, Decision::Disabled,
                  "suspended VT disables VRR");
  input = eligible();
  input.display.output_configuration_busy = true;
  require_blocked(input, Reason::OutputConfigurationBusy, Decision::Disabled,
                  "output reconfiguration disables VRR");
  input = eligible();
  input.output.kms_controllable = false;
  require_blocked(input, Reason::PresenterRejected, Decision::Rejected,
                  "uncontrollable presenter rejects VRR");
  input = eligible();
  input.presenter.accepted = false;
  require_blocked(input, Reason::PresenterRejected, Decision::Rejected,
                  "presenter rejection is reported");
  input = eligible();
  input.presenter.property_readback_matches = false;
  require_blocked(input, Reason::PropertyReadbackMismatch, Decision::Rejected,
                  "property readback mismatch is rejected");

  input = eligible();
  input.presenter.timing_available = false;
  auto result = evaluate(input);
  require(result.desired_enabled &&
              has_reason(result.reasons, Reason::TimingUnavailable),
          "missing timing is informational and does not prevent observation");
  input = eligible();
  input.presenter.hardware_behavior_confirmed = false;
  result = evaluate(input);
  require(result.desired_enabled &&
              has_reason(result.reasons, Reason::HardwareBehaviorUnconfirmed),
          "unconfirmed hardware behavior remains informational");
}

void test_candidate_window_and_surface_reasons() {
  auto input = eligible();
  input.candidate.selected = false;
  require_blocked(input, Reason::NoCandidate, Decision::Disabled,
                  "candidate absence is explicit");

  input = eligible();
  input.candidate.window_present = false;
  require_blocked(input, Reason::WindowMissing, Decision::Disabled,
                  "candidate disappearance is explicit");
  input = eligible();
  input.candidate.visible = false;
  require_blocked(input, Reason::WindowHidden, Decision::Disabled,
                  "hidden window is rejected");
  input = eligible();
  input.candidate.managed = false;
  require_blocked(input, Reason::WindowUnmanaged, Decision::Disabled,
                  "unmanaged window is rejected");
  input = eligible();
  input.candidate.focused = false;
  require_blocked(input, Reason::WindowUnfocused, Decision::Disabled,
                  "unfocused window is rejected");
  input = eligible(PolicyMode::Fullscreen);
  input.candidate.fullscreen = false;
  require_blocked(input, Reason::WindowNotFullscreen, Decision::Disabled,
                  "non-fullscreen window is rejected");
  require(has_reason(evaluate(input).reasons,
                     Reason::WindowNotBorderlessFullscreen),
          "failed fullscreen alternatives retain both diagnostics");
  input = eligible();
  input.candidate.exclusive_output_membership = false;
  require_blocked(input, Reason::WindowSpansOutputs, Decision::Disabled,
                  "spanning window is rejected");
  input = eligible();
  input.candidate.preference = WindowPreference::Disable;
  require_blocked(input, Reason::WindowPreferenceDisabled, Decision::Disabled,
                  "application opt-out rejects candidate policy");
  input = eligible();
  input.candidate.preference = static_cast<WindowPreference>(4);
  require_blocked(input, Reason::WindowPreferenceDisabled, Decision::Disabled,
                  "unknown application preference fails closed");

  input = eligible();
  input.candidate.surface_present = false;
  require_blocked(input, Reason::SurfaceMissing, Decision::Disabled,
                  "missing surface is rejected");
  input = eligible();
  input.candidate.surface_metadata_only = true;
  require_blocked(input, Reason::SurfaceMetadataOnly, Decision::Disabled,
                  "metadata-only surface is rejected");
  input = eligible();
  input.candidate.surface_visible = false;
  require_blocked(input, Reason::SurfaceNotVisible, Decision::Disabled,
                  "invisible surface is rejected");
  input = eligible();
  input.candidate.surface_opaque = false;
  require_blocked(input, Reason::SurfaceNotOpaque, Decision::Disabled,
                  "transparent surface is rejected");
  input = eligible();
  input.candidate.surface_on_output = false;
  require_blocked(input, Reason::SurfaceOnWrongOutput, Decision::Disabled,
                  "surface on another output is rejected");
  input = eligible();
  input.candidate.surface_membership_valid = false;
  require_blocked(input, Reason::SurfaceMembershipInvalid, Decision::Disabled,
                  "invalid surface membership is rejected");
}

void test_simulation_reason_stability_and_precedence() {
  auto input = eligible(PolicyMode::Fullscreen);
  input.output = {true, true, false, false, false, true, false, true, true};
  input.presenter.hardware_behavior_confirmed = false;
  auto result = evaluate(input);
  require(result.desired_enabled && result.decision == Decision::Enabled &&
              has_reason(result.reasons, Reason::SimulatedHeadless) &&
              !has_reason(result.reasons, Reason::OutputNotDrm) &&
              !has_reason(result.reasons, Reason::OutputNotVrrCapable) &&
              !has_reason(result.reasons, Reason::AtomicKmsUnavailable) &&
              !has_reason(result.reasons, Reason::HardwareBehaviorUnconfirmed),
          "simulated headless is controllable without hardware claims");

  input = eligible(PolicyMode::Off);
  input.output.enabled = false;
  input.candidate.selected = false;
  input.candidate.visible = false;
  result = evaluate(input);
  require(
      result.primary == Reason::OutputDisabled &&
          has_reason(result.reasons, Reason::PolicyOff) &&
          !has_reason(result.reasons, Reason::NoCandidate),
      "primary reason follows output before policy and Off skips candidate");

  std::array<bool, kReasonCount> seen{};
  for (const auto reason : reason_precedence()) {
    const auto index = static_cast<std::size_t>(reason);
    require(index < seen.size() && !seen[index] &&
                primary_reason(reason_bit(reason)) == reason &&
                !reason_name(reason).empty(),
            "reason table contains every stable reason exactly once");
    seen[index] = true;
  }
  for (const bool present : seen)
    require(present, "all 33 stable reason bits are represented");
  require(valid_reason_mask(kKnownReasonMask) &&
              !valid_reason_mask(UINT64_C(1) << 33U),
          "reason mask accepts exactly the stable 33 bits");
  require(valid_policy_mode(PolicyMode::AlwaysEligible) &&
              !valid_policy_mode(static_cast<PolicyMode>(0)) &&
              valid_window_preference(WindowPreference::Default) &&
              !valid_window_preference(static_cast<WindowPreference>(4)),
          "policy and preference domains are exact");
}

} // namespace

int main() {
  test_modes_and_candidate_semantics();
  test_output_session_and_presenter_reasons();
  test_candidate_window_and_surface_reasons();
  test_simulation_reason_stability_and_precedence();
  return 0;
}
