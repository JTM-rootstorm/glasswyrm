#include "wm/multi_output_policy.hpp"
#include "wm/policy_engine_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

namespace glasswyrm::wm::detail {
namespace {

AppliedState applied_state(const RawWindow& window) {
  if (window.override_redirect) return AppliedState::Normal;
  if (window.minimized_requested) return AppliedState::Minimized;
  if (window.fullscreen_requested) return AppliedState::Fullscreen;
  if (window.maximized_requested) return AppliedState::Maximized;
  return AppliedState::Normal;
}

bool decorated(const RawWindow& window, const AppliedState state) {
  if (window.override_redirect || state == AppliedState::Fullscreen ||
      window.decoration_preference == DecorationPreference::False)
    return false;
  if (window.window_type == WindowType::Utility ||
      window.window_type == WindowType::Unknown)
    return window.decoration_preference == DecorationPreference::True;
  return true;
}

Rectangle requested_geometry(const RawWindow& window) {
  return {window.requested_x, window.requested_y, window.requested_width,
          window.requested_height};
}

WindowOutputHint hint_for(const RawState& raw, const std::uint32_t id) {
  const auto found = raw.output_hints.find(id);
  return found == raw.output_hints.end() ? WindowOutputHint{} : found->second;
}

bool valid_output(const RawState& raw, const std::uint64_t id) {
  const auto found = raw.outputs.find(id);
  return found != raw.outputs.end() && found->second.enabled;
}

std::uint64_t initial_output(const RawState& raw,
                             const WindowOutputHint& hint) {
  if (valid_output(raw, hint.preferred_output_id))
    return hint.preferred_output_id;
  if (valid_output(raw, raw.context.output_id)) return raw.context.output_id;
  for (const auto& [id, output] : raw.outputs)
    if (output.enabled) return id;
  return 0;
}

bool cascade_candidate(const RawWindow& window,
                       const AppliedState state) {
  return !window.override_redirect && window.transient_for == 0 &&
         window.geometry_serial == 0 && state == AppliedState::Normal &&
         (window.window_type == WindowType::Normal ||
          window.window_type == WindowType::Utility ||
          window.window_type == WindowType::Unknown);
}

void apply_geometry(WindowState& state, const Rectangle& geometry) {
  state.final_x = geometry.x;
  state.final_y = geometry.y;
  state.final_width = geometry.width;
  state.final_height = geometry.height;
}

std::uint64_t assign_root(const RawState& raw, const RawWindow& window,
                          const WindowState& state,
                          const WindowOutputHint& hint) {
  if (cascade_candidate(window, state.applied_state))
    return initial_output(raw, hint);
  return select_output(
      raw.outputs, raw.context.output_id,
      {requested_geometry(window), 0, hint.previous_output_id,
       hint.preferred_output_id,
       state.applied_state == AppliedState::Fullscreen ||
           state.applied_state == AppliedState::Maximized});
}

Rectangle root_geometry(const RawState& raw, const RawWindow& window,
                        const WindowState& state, const std::uint64_t output_id,
                        const std::size_t cascade_slot) {
  const auto& output = raw.outputs.at(output_id);
  if (state.applied_state == AppliedState::Fullscreen)
    return fullscreen_geometry(output);
  if (state.applied_state == AppliedState::Maximized)
    return maximize_geometry(output);
  if (window.override_redirect) return requested_geometry(window);
  if (window.geometry_serial == 0)
    return initial_placement(output, window.requested_width,
                             window.requested_height, cascade_slot);
  return retain_visible_pixel(raw.outputs, output_id,
                              requested_geometry(window));
}

std::vector<std::uint32_t> root_order(const RawState& raw) {
  std::vector<std::uint32_t> ids;
  for (const auto& [id, window] : raw.windows)
    if (window.override_redirect || window.transient_for == 0)
      ids.push_back(id);
  std::sort(ids.begin(), ids.end(), [&](const auto left, const auto right) {
    return std::tie(raw.windows.at(left).creation_serial, left) <
           std::tie(raw.windows.at(right).creation_serial, right);
  });
  return ids;
}

void place_transient(const RawState& raw, const std::uint32_t id,
                     PolicyState& policy) {
  const auto& window = raw.windows.at(id);
  auto& state = policy.windows.at(id);
  const auto& parent = policy.windows.at(window.transient_for);
  state.output_id = parent.output_id;
  const auto& output = raw.outputs.at(state.output_id);
  if (state.applied_state == AppliedState::Fullscreen) {
    apply_geometry(state, fullscreen_geometry(output));
    return;
  }
  if (state.applied_state == AppliedState::Maximized) {
    apply_geometry(state, maximize_geometry(output));
    return;
  }
  Rectangle geometry;
  geometry.width = std::min(window.requested_width, output.work.width);
  geometry.height = std::min(window.requested_height, output.work.height);
  geometry.x = static_cast<std::int32_t>(
      static_cast<std::int64_t>(parent.final_x) +
      (static_cast<std::int64_t>(parent.final_width) - geometry.width) / 2);
  geometry.y = static_cast<std::int32_t>(
      static_cast<std::int64_t>(parent.final_y) +
      (static_cast<std::int64_t>(parent.final_height) - geometry.height) / 2);
  apply_geometry(state,
                 retain_visible_pixel(raw.outputs, state.output_id, geometry));
}

}  // namespace

void apply_multi_output_geometry(const RawState& raw, PolicyState& policy) {
  policy.outputs = raw.outputs;
  policy.output_hints = raw.output_hints;
  for (const auto& [id, window] : raw.windows) {
    WindowState state;
    state.window_id = id;
    state.transient_for = window.transient_for;
    state.workspace_id = raw.context.workspace_id;
    state.window_type = window.window_type;
    state.applied_state = applied_state(window);
    state.managed = !window.override_redirect;
    state.override_redirect = window.override_redirect;
    state.attention_requested = window.attention_requested;
    state.decoration_eligible = decorated(window, state.applied_state);
    policy.windows.emplace(id, state);
  }

  std::map<std::uint64_t, std::size_t> cascade_slots;
  for (const auto id : root_order(raw)) {
    const auto& window = raw.windows.at(id);
    auto& state = policy.windows.at(id);
    state.output_id = assign_root(raw, window, state, hint_for(raw, id));
    auto slot = std::size_t{0};
    if (cascade_candidate(window, state.applied_state) && window.wants_map)
      slot = cascade_slots[state.output_id]++;
    apply_geometry(state, root_geometry(raw, window, state, state.output_id,
                                        slot));
  }
  for (const auto id : transient_parent_first(raw))
    place_transient(raw, id, policy);
}

}  // namespace glasswyrm::wm::detail
