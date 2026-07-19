#include "wm/vrr_policy.hpp"

#include "wm/vrr_hash.hpp"
#include "wm/vrr_validation.hpp"

namespace glasswyrm::wm {
namespace {

bool same_rectangle(const WindowState& window,
                    const Rectangle& rectangle) noexcept {
  return window.final_x == rectangle.x && window.final_y == rectangle.y &&
         window.final_width == rectangle.width &&
         window.final_height == rectangle.height;
}

bool exclusive_membership(const VrrWindowInput& input,
                          const std::uint64_t output_id) noexcept {
  return input.output_membership.size() == 1 &&
         input.output_membership.front() == output_id;
}

std::uint64_t membership_reasons(const VrrWindowInput& input,
                                 const std::uint64_t output_id) noexcept {
  if (input.output_membership.size() > 1)
    return vrr_reason::window_spans_outputs;
  if (!exclusive_membership(input, output_id))
    return vrr_reason::surface_membership_invalid;
  return 0;
}

VrrWindowState classify_window(const RawState& raw, const PolicyState& base,
                               const VrrWindowInput& input) noexcept {
  const auto& window = base.windows.at(input.window_id);
  VrrWindowState result;
  result.window_id = input.window_id;
  result.output_id = window.output_id;
  result.preference = input.preference;
  result.visible = window.visible;
  result.focused = window.focused;
  result.fullscreen = window.applied_state == AppliedState::Fullscreen;
  result.exclusive_output_membership =
      exclusive_membership(input, window.output_id);
  result.borderless_fullscreen =
      classify_borderless_fullscreen(raw, base, input, input.window_id);

  if (!window.managed || window.override_redirect)
    result.reason_flags |= vrr_reason::window_unmanaged;
  if (!window.visible)
    result.reason_flags |= vrr_reason::window_hidden;
  if (!window.focused)
    result.reason_flags |= vrr_reason::window_unfocused;
  result.reason_flags |= membership_reasons(input, window.output_id);
  if (input.preference == VrrWindowPreference::Disable)
    result.reason_flags |= vrr_reason::window_preference_disabled;
  return result;
}

bool common_candidate(const VrrWindowState& window) noexcept {
  return window.visible && window.focused &&
         window.exclusive_output_membership &&
         (window.reason_flags & vrr_reason::window_unmanaged) == 0;
}

void apply_mode(VrrWindowState& window, const VrrPolicyMode mode) noexcept {
  const bool common = common_candidate(window);
  switch (mode) {
    case VrrPolicyMode::Off:
      window.reason_flags |= vrr_reason::policy_off;
      window.eligible = false;
      break;
    case VrrPolicyMode::Fullscreen:
      if (!window.fullscreen)
        window.reason_flags |= vrr_reason::window_not_fullscreen;
      if (!window.fullscreen && !window.borderless_fullscreen)
        window.reason_flags |=
            vrr_reason::window_not_borderless_fullscreen;
      window.eligible = common &&
                        (window.fullscreen || window.borderless_fullscreen) &&
                        window.preference != VrrWindowPreference::Disable;
      break;
    case VrrPolicyMode::Focused:
      window.eligible =
          common && window.preference != VrrWindowPreference::Disable;
      break;
    case VrrPolicyMode::AppRequested:
      if (window.preference != VrrWindowPreference::Prefer)
        window.reason_flags |= vrr_reason::window_did_not_request;
      window.eligible = common &&
                        window.preference == VrrWindowPreference::Prefer;
      break;
    case VrrPolicyMode::AlwaysEligible:
      // This is a diagnostic candidate only. The manual output policy does not
      // require a window and intentionally ignores application opt-out.
      window.eligible = common;
      break;
  }
}

bool candidate_required(const VrrPolicyMode mode) noexcept {
  return mode == VrrPolicyMode::Fullscreen ||
         mode == VrrPolicyMode::Focused ||
         mode == VrrPolicyMode::AppRequested;
}

}  // namespace

bool classify_borderless_fullscreen(const RawState& raw,
                                    const PolicyState& base,
                                    const VrrWindowInput& input,
                                    const std::uint32_t window_id) noexcept {
  const auto raw_window = raw.windows.find(window_id);
  const auto window = base.windows.find(window_id);
  if (raw_window == raw.windows.end() || window == base.windows.end())
    return false;
  const auto output = base.outputs.find(window->second.output_id);
  if (output == base.outputs.end() || !output->second.enabled)
    return false;
  const bool transient_on_other_output =
      raw_window->second.transient_for != 0 &&
      (!base.windows.contains(raw_window->second.transient_for) ||
       base.windows.at(raw_window->second.transient_for).output_id !=
           window->second.output_id);
  return raw_window->second.parent_window_id == raw.context.root_window_id &&
         window->second.managed && !window->second.override_redirect &&
         window->second.visible && window->second.focused &&
         window->second.applied_state != AppliedState::Minimized &&
         (raw_window->second.decoration_preference ==
              DecorationPreference::False ||
          !window->second.decoration_eligible) &&
         raw_window->second.border_width == 0 &&
         same_rectangle(window->second, output->second.logical) &&
         exclusive_membership(input, window->second.output_id) &&
         !transient_on_other_output;
}

VrrEvaluation evaluate_vrr_policy(const RawState& raw,
                                  const PolicyState& base,
                                  const VrrInputs& inputs) {
  VrrEvaluation evaluation;
  evaluation.error = validate_vrr_inputs(raw, base, inputs);
  if (evaluation.error != VrrEvaluationError::None)
    return evaluation;

  auto& policy = evaluation.policy;
  policy.generation = base.generation;
  policy.base_policy_hash = base.hash;

  for (const auto& [id, input] : inputs.windows) {
    auto result = classify_window(raw, base, input);
    apply_mode(result, inputs.outputs.at(result.output_id).mode);
    policy.windows.emplace(id, result);
  }

  for (const auto& [id, input] : inputs.outputs) {
    const auto& output = base.outputs.at(id);
    VrrOutputState result;
    result.output_id = id;
    result.mode = input.mode;
    result.candidate_required = candidate_required(input.mode);
    if (!output.enabled)
      result.reason_flags |= vrr_reason::output_disabled;
    if (!input.hardware_capable)
      result.reason_flags |= vrr_reason::output_not_vrr_capable;
    if (!input.kms_controllable)
      result.reason_flags |= vrr_reason::atomic_kms_unavailable;
    if (input.mode == VrrPolicyMode::Off)
      result.reason_flags |= vrr_reason::policy_off;
    if (input.mode == VrrPolicyMode::AlwaysEligible)
      result.reason_flags |= vrr_reason::manual_always_eligible;

    for (auto& [window_id, window] : policy.windows) {
      if (window.output_id == id && window.eligible && window.focused) {
        result.selected_window_id = window_id;
        window.selected = true;
        break;
      }
    }
    if (result.candidate_required && result.selected_window_id == 0)
      result.reason_flags |= vrr_reason::no_candidate;

    const bool output_available = output.enabled && input.kms_controllable;
    result.desired_enabled =
        output_available && input.mode != VrrPolicyMode::Off &&
        (!result.candidate_required || result.selected_window_id != 0);
    policy.outputs.emplace(id, result);
  }

  policy.hash = vrr_policy_hash(base, inputs, policy);
  return evaluation;
}

}  // namespace glasswyrm::wm
