#include "wm/policy_engine_internal.hpp"

#include <algorithm>
#include <tuple>

namespace glasswyrm::wm::detail {
namespace {

void apply_visibility(const RawState& raw, PolicyState& policy) {
  for (const auto& [id, window] : raw.windows) {
    auto& state = policy.windows.at(id);
    state.visible = window.wants_map &&
                    (window.override_redirect ||
                     state.applied_state != AppliedState::Minimized);
  }
  for (const auto id : transient_parent_first(raw)) {
    auto& state = policy.windows.at(id);
    state.visible =
        state.visible && policy.windows.at(state.transient_for).visible;
  }
  for (auto& [id, state] : policy.windows) {
    (void)id;
    state.fullscreen_eligible = state.override_redirect
                                    ? TriState::Unknown
                                    : (state.visible &&
                                               state.applied_state ==
                                                   AppliedState::Fullscreen
                                           ? TriState::True
                                           : TriState::False);
  }
}

void apply_focus(const RawState& raw, PolicyState& policy) {
  std::vector<std::uint32_t> candidates;
  for (const auto& [id, window] : raw.windows) {
    if (policy.windows.at(id).visible && !window.override_redirect &&
        (window.flags & kInputDisabledFlag) == 0)
      candidates.push_back(id);
  }
  if (candidates.empty()) return;

  const bool has_explicit =
      std::any_of(candidates.begin(), candidates.end(), [&](const auto id) {
        return raw.windows.at(id).focus_serial != 0;
      });
  const bool has_fullscreen =
      std::any_of(candidates.begin(), candidates.end(), [&](const auto id) {
        return policy.windows.at(id).applied_state == AppliedState::Fullscreen;
      });
  const auto focus_rank = [&](const std::uint32_t id) {
    const auto& window = raw.windows.at(id);
    if (has_fullscreen && window.transient_for != 0) return 2;
    return policy.windows.at(id).applied_state == AppliedState::Fullscreen ? 1
                                                                           : 0;
  };
  const auto before = [&](const auto left, const auto right) {
    const auto& l = raw.windows.at(left);
    const auto& r = raw.windows.at(right);
    if (has_explicit)
      return std::tuple(focus_rank(left), l.focus_serial, l.map_serial,
                        l.creation_serial, l.window_id) <
             std::tuple(focus_rank(right), r.focus_serial, r.map_serial,
                        r.creation_serial, r.window_id);
    return std::tuple(focus_rank(left), l.map_serial, l.creation_serial,
                      l.window_id) <
           std::tuple(focus_rank(right), r.map_serial, r.creation_serial,
                      r.window_id);
  };
  const auto selected =
      *std::max_element(candidates.begin(), candidates.end(), before);
  policy.windows.at(selected).focused = true;
}

void apply_presentation_eligibility(PolicyState& policy) {
  for (auto& [id, state] : policy.windows) {
    (void)id;
    if (!state.override_redirect &&
        state.fullscreen_eligible == TriState::True && !state.focused) {
      state.direct_scanout_eligible = TriState::False;
    } else {
      // GWM cannot see the compositor's opacity, format, or occlusion state.
      // Preserve that uncertainty instead of claiming direct scanout support.
      state.direct_scanout_eligible = TriState::Unknown;
    }
  }
}

}  // namespace

void apply_focus_and_visibility(const RawState& raw, PolicyState& policy) {
  apply_visibility(raw, policy);
  apply_focus(raw, policy);
  apply_presentation_eligibility(policy);
}

}  // namespace glasswyrm::wm::detail
