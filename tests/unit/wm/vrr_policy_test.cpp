#include "wm/vrr_policy.hpp"

#include "tests/helpers/test_support.hpp"
#include "tests/unit/wm/vrr_test_fixture.hpp"

namespace {

using namespace glasswyrm::wm;
using glasswyrm::wm::test::fixture;
using gw::test::require;

void borderless_fullscreen_classification() {
  auto value = fixture();
  require(classify_borderless_fullscreen(
              value.raw, value.base, value.inputs.windows.at(1001), 1001),
          "exact undecorated logical-output geometry is borderless fullscreen");

  value.base.windows.at(1001).applied_state = AppliedState::Maximized;
  value.base.windows.at(1001).final_height = 560;
  require(!classify_borderless_fullscreen(
              value.raw, value.base, value.inputs.windows.at(1001), 1001),
          "maximized work-area geometry is not borderless fullscreen");

  value = fixture();
  value.raw.windows.at(1001).border_width = 1;
  require(!classify_borderless_fullscreen(
              value.raw, value.base, value.inputs.windows.at(1001), 1001),
          "a nonzero X border excludes borderless fullscreen");

  value = fixture();
  value.raw.windows.at(1001).decoration_preference =
      DecorationPreference::True;
  value.base.windows.at(1001).decoration_eligible = true;
  require(!classify_borderless_fullscreen(
              value.raw, value.base, value.inputs.windows.at(1001), 1001),
          "decoration intent and eligibility exclude borderless fullscreen");

  value = fixture();
  value.inputs.windows.at(1001).output_membership = {10, 20};
  require(!classify_borderless_fullscreen(
              value.raw, value.base, value.inputs.windows.at(1001), 1001),
          "spanning membership excludes borderless fullscreen");

  value = fixture();
  value.raw.windows.emplace(1002, test::raw_window(1002));
  value.base.windows.emplace(1002, test::policy_window(1002, 20));
  value.raw.windows.at(1001).transient_for = 1002;
  require(!classify_borderless_fullscreen(
              value.raw, value.base, value.inputs.windows.at(1001), 1001),
          "a transient with a parent on another output is excluded");
}

void off_and_fullscreen_modes() {
  auto value = fixture();
  auto evaluated = evaluate_vrr_policy(value.raw, value.base, value.inputs);
  require(evaluated && !evaluated.policy.outputs.at(10).desired_enabled &&
              evaluated.policy.outputs.at(10).selected_window_id == 0 &&
              evaluated.policy.outputs.at(10).reason_flags ==
                  vrr_reason::policy_off &&
              evaluated.policy.windows.at(1001).reason_flags ==
                  vrr_reason::policy_off,
          "Off disables output and window selection with stable reasons");

  value.inputs.outputs.at(10).mode = VrrPolicyMode::Fullscreen;
  evaluated = evaluate_vrr_policy(value.raw, value.base, value.inputs);
  require(evaluated && evaluated.policy.outputs.at(10).desired_enabled &&
              evaluated.policy.outputs.at(10).selected_window_id == 1001 &&
              evaluated.policy.windows.at(1001).borderless_fullscreen &&
              evaluated.policy.windows.at(1001).selected,
          "Fullscreen selects an exact borderless-fullscreen window");

  value = fixture();
  value.inputs.outputs.at(10).mode = VrrPolicyMode::Fullscreen;
  auto& window = value.base.windows.at(1001);
  window.applied_state = AppliedState::Fullscreen;
  window.final_height = 560;
  evaluated = evaluate_vrr_policy(value.raw, value.base, value.inputs);
  require(evaluated && evaluated.policy.outputs.at(10).desired_enabled &&
              evaluated.policy.windows.at(1001).fullscreen &&
              !evaluated.policy.windows.at(1001).borderless_fullscreen,
          "GWM-applied fullscreen remains eligible on the work rectangle");

  window.applied_state = AppliedState::Normal;
  evaluated = evaluate_vrr_policy(value.raw, value.base, value.inputs);
  const auto expected = vrr_reason::window_not_fullscreen |
                        vrr_reason::window_not_borderless_fullscreen;
  require(evaluated && !evaluated.policy.outputs.at(10).desired_enabled &&
              evaluated.policy.outputs.at(10).reason_flags ==
                  vrr_reason::no_candidate &&
              evaluated.policy.windows.at(1001).reason_flags == expected,
          "windowed geometry is rejected with exact fullscreen reasons");
}

void focused_and_application_modes() {
  auto value = fixture();
  value.base.windows.at(1001).final_width = 400;
  value.base.windows.at(1001).final_height = 300;
  value.inputs.outputs.at(10).mode = VrrPolicyMode::Focused;
  auto evaluated = evaluate_vrr_policy(value.raw, value.base, value.inputs);
  require(evaluated && evaluated.policy.outputs.at(10).desired_enabled &&
              evaluated.policy.outputs.at(10).selected_window_id == 1001,
          "Focused accepts a focused eligible windowed window");

  value.inputs.windows.at(1001).preference = VrrWindowPreference::Disable;
  evaluated = evaluate_vrr_policy(value.raw, value.base, value.inputs);
  require(evaluated && !evaluated.policy.outputs.at(10).desired_enabled &&
              evaluated.policy.windows.at(1001).reason_flags ==
                  vrr_reason::window_preference_disabled,
          "Disable opts a window out of candidate-based Focused policy");

  value.inputs.outputs.at(10).mode = VrrPolicyMode::AppRequested;
  value.inputs.windows.at(1001).preference = VrrWindowPreference::Default;
  evaluated = evaluate_vrr_policy(value.raw, value.base, value.inputs);
  require(evaluated && !evaluated.policy.outputs.at(10).desired_enabled &&
              evaluated.policy.windows.at(1001).reason_flags ==
                  vrr_reason::window_did_not_request,
          "AppRequested rejects Default with an exact reason");

  value.inputs.windows.at(1001).preference = VrrWindowPreference::Allow;
  evaluated = evaluate_vrr_policy(value.raw, value.base, value.inputs);
  require(evaluated && !evaluated.policy.outputs.at(10).desired_enabled &&
              evaluated.policy.windows.at(1001).reason_flags ==
                  vrr_reason::window_did_not_request,
          "AppRequested does not treat Allow as an explicit request");

  value.inputs.windows.at(1001).preference = VrrWindowPreference::Prefer;
  evaluated = evaluate_vrr_policy(value.raw, value.base, value.inputs);
  require(evaluated && evaluated.policy.outputs.at(10).desired_enabled &&
              evaluated.policy.windows.at(1001).reason_flags == 0,
          "AppRequested selects an eligible Prefer window");

  value.inputs.windows.at(1001).preference = VrrWindowPreference::Disable;
  evaluated = evaluate_vrr_policy(value.raw, value.base, value.inputs);
  require(evaluated && !evaluated.policy.outputs.at(10).desired_enabled &&
              evaluated.policy.windows.at(1001).reason_flags ==
                  (vrr_reason::window_preference_disabled |
                   vrr_reason::window_did_not_request),
          "AppRequested preserves explicit opt-out and no-request reasons");
}

void manual_and_capability_modes() {
  auto value = fixture();
  value.inputs.outputs.at(10).mode = VrrPolicyMode::AlwaysEligible;
  value.inputs.windows.at(1001).preference = VrrWindowPreference::Disable;
  auto evaluated = evaluate_vrr_policy(value.raw, value.base, value.inputs);
  require(evaluated && evaluated.policy.outputs.at(10).desired_enabled &&
              !evaluated.policy.outputs.at(10).candidate_required &&
              evaluated.policy.outputs.at(10).selected_window_id == 1001 &&
              evaluated.policy.outputs.at(10).reason_flags ==
                  vrr_reason::manual_always_eligible &&
              evaluated.policy.windows.at(1001).reason_flags ==
                  vrr_reason::window_preference_disabled,
          "AlwaysEligible ignores opt-out but makes the override observable");

  value.base.windows.at(1001).focused = false;
  evaluated = evaluate_vrr_policy(value.raw, value.base, value.inputs);
  require(evaluated && evaluated.policy.outputs.at(10).desired_enabled &&
              evaluated.policy.outputs.at(10).selected_window_id == 0 &&
              evaluated.policy.windows.at(1001).reason_flags ==
                  (vrr_reason::window_unfocused |
                   vrr_reason::window_preference_disabled),
          "AlwaysEligible needs no diagnostic candidate");

  value = fixture();
  value.inputs.outputs.at(10).mode = VrrPolicyMode::Focused;
  value.inputs.outputs.at(10).hardware_capable = false;
  evaluated = evaluate_vrr_policy(value.raw, value.base, value.inputs);
  require(evaluated && evaluated.policy.outputs.at(10).desired_enabled &&
              evaluated.policy.outputs.at(10).reason_flags ==
                  vrr_reason::output_not_vrr_capable,
          "simulated controllability stays eligible without hardware claims");

  value.inputs.outputs.at(10).kms_controllable = false;
  evaluated = evaluate_vrr_policy(value.raw, value.base, value.inputs);
  require(evaluated && !evaluated.policy.outputs.at(10).desired_enabled &&
              evaluated.policy.outputs.at(10).selected_window_id == 1001 &&
              evaluated.policy.outputs.at(10).reason_flags ==
                  (vrr_reason::output_not_vrr_capable |
                   vrr_reason::atomic_kms_unavailable),
          "an uncontrollable output keeps its candidate and stable reasons");
}

void membership_and_focus_reasons() {
  auto value = fixture();
  value.inputs.outputs.at(10).mode = VrrPolicyMode::Focused;
  value.inputs.windows.at(1001).output_membership.clear();
  auto evaluated = evaluate_vrr_policy(value.raw, value.base, value.inputs);
  require(evaluated && !evaluated.policy.outputs.at(10).desired_enabled &&
              evaluated.policy.windows.at(1001).reason_flags ==
                  vrr_reason::surface_membership_invalid,
          "missing membership has a stable invalid-membership reason");

  value.inputs.windows.at(1001).output_membership = {20, 10};
  evaluated = evaluate_vrr_policy(value.raw, value.base, value.inputs);
  require(evaluated && !evaluated.policy.outputs.at(10).desired_enabled &&
              evaluated.policy.windows.at(1001).reason_flags ==
                  vrr_reason::window_spans_outputs,
          "spanning membership has a stable spanning reason");

  value.inputs.windows.at(1001).output_membership = {10};
  value.base.windows.at(1001).focused = false;
  evaluated = evaluate_vrr_policy(value.raw, value.base, value.inputs);
  require(evaluated && !evaluated.policy.outputs.at(10).desired_enabled &&
              evaluated.policy.windows.at(1001).reason_flags ==
                  vrr_reason::window_unfocused,
          "an unfocused window has the exact focus reason");

  value.base.windows.at(1001).focused = true;
  value.base.windows.at(1001).visible = false;
  evaluated = evaluate_vrr_policy(value.raw, value.base, value.inputs);
  require(evaluated && !evaluated.policy.outputs.at(10).desired_enabled &&
              evaluated.policy.windows.at(1001).reason_flags ==
                  vrr_reason::window_hidden,
          "a hidden focused window has the exact visibility reason");
}

}  // namespace

int main() {
  borderless_fullscreen_classification();
  off_and_fullscreen_modes();
  focused_and_application_modes();
  manual_and_capability_modes();
  membership_and_focus_reasons();
  return 0;
}
