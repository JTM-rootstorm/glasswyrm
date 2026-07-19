#include "wm/policy_engine_internal.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <tuple>

namespace glasswyrm::wm::detail {
namespace {

AppliedState applied_state(const RawWindow& raw) {
  if (raw.override_redirect) return AppliedState::Normal;
  if (raw.minimized_requested) return AppliedState::Minimized;
  if (raw.fullscreen_requested) return AppliedState::Fullscreen;
  if (raw.maximized_requested) return AppliedState::Maximized;
  return AppliedState::Normal;
}

bool decoration(const RawWindow& raw, const AppliedState state) {
  if (raw.override_redirect || state == AppliedState::Fullscreen) return false;
  if (raw.decoration_preference == DecorationPreference::False) return false;
  if (raw.window_type == WindowType::Utility ||
      raw.window_type == WindowType::Unknown)
    return raw.decoration_preference == DecorationPreference::True;
  return true;
}

std::int32_t clamp_coordinate(const std::int64_t value,
                              const std::int32_t low,
                              const std::int64_t high) {
  return static_cast<std::int32_t>(
      std::clamp(value, static_cast<std::int64_t>(low), high));
}

std::map<std::uint32_t, std::size_t> cascade_slots(const RawState& raw) {
  std::vector<const RawWindow*> cascade;
  for (const auto& [id, window] : raw.windows) {
    (void)id;
    if (!window.override_redirect && window.transient_for == 0 &&
        window.wants_map && !window.fullscreen_requested &&
        !window.maximized_requested &&
        (window.window_type == WindowType::Normal ||
         window.window_type == WindowType::Utility ||
         window.window_type == WindowType::Unknown))
      cascade.push_back(&window);
  }
  std::sort(cascade.begin(), cascade.end(), [](const auto* left,
                                                const auto* right) {
    return std::tie(left->creation_serial, left->window_id) <
           std::tie(right->creation_serial, right->window_id);
  });
  std::map<std::uint32_t, std::size_t> slots;
  for (std::size_t index = 0; index < cascade.size(); ++index)
    slots.emplace(cascade[index]->window_id, index);
  return slots;
}

void place_root(const RawState& raw, const RawWindow& window,
                const std::map<std::uint32_t, std::size_t>& slots,
                WindowState& state) {
  if (window.override_redirect) {
    state.final_x = window.requested_x;
    state.final_y = window.requested_y;
  } else if (state.applied_state == AppliedState::Fullscreen ||
             state.applied_state == AppliedState::Maximized) {
    state.final_x = raw.context.work_x;
    state.final_y = raw.context.work_y;
    state.final_width = raw.context.work_width;
    state.final_height = raw.context.work_height;
  } else if (window.transient_for == 0 && window.geometry_serial != 0) {
    const auto maximum_x = static_cast<std::int64_t>(raw.context.work_x) +
                           raw.context.work_width - state.final_width;
    const auto maximum_y = static_cast<std::int64_t>(raw.context.work_y) +
                           raw.context.work_height - state.final_height;
    state.final_x =
        clamp_coordinate(window.requested_x, raw.context.work_x, maximum_x);
    state.final_y =
        clamp_coordinate(window.requested_y, raw.context.work_y, maximum_y);
  } else if (window.transient_for == 0) {
    const auto slot = slots.contains(window.window_id)
                          ? slots.at(window.window_id)
                          : 0;
    const auto x_span = raw.context.work_width - state.final_width;
    const auto y_span = raw.context.work_height - state.final_height;
    state.final_x = raw.context.work_x + static_cast<std::int32_t>(
        x_span == 0 ? 0
                    : (slot * 32U) %
                          (static_cast<std::size_t>(x_span) + 1U));
    state.final_y = raw.context.work_y + static_cast<std::int32_t>(
        y_span == 0 ? 0
                    : (slot * 32U) %
                          (static_cast<std::size_t>(y_span) + 1U));
  }
}

void place_transient(const RawState& raw, const std::uint32_t id,
                     PolicyState& policy) {
  const auto& raw_window = raw.windows.at(id);
  auto& state = policy.windows.at(id);
  const auto& parent = policy.windows.at(raw_window.transient_for);
  const std::int64_t centered_x = static_cast<std::int64_t>(parent.final_x) +
      (static_cast<std::int64_t>(parent.final_width) - state.final_width) / 2;
  const std::int64_t centered_y = static_cast<std::int64_t>(parent.final_y) +
      (static_cast<std::int64_t>(parent.final_height) - state.final_height) / 2;
  state.final_x = clamp_coordinate(
      centered_x, raw.context.work_x,
      static_cast<std::int64_t>(raw.context.work_x) + raw.context.work_width -
          state.final_width);
  state.final_y = clamp_coordinate(
      centered_y, raw.context.work_y,
      static_cast<std::int64_t>(raw.context.work_y) + raw.context.work_height -
          state.final_height);
}

}  // namespace

void apply_placement_and_fullscreen_geometry(const RawState& raw,
                                             PolicyState& policy) {
  const auto slots = cascade_slots(raw);
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
    state.final_width = window.override_redirect
                            ? window.requested_width
                            : std::min(window.requested_width,
                                       raw.context.work_width);
    state.final_height = window.override_redirect
                             ? window.requested_height
                             : std::min(window.requested_height,
                                        raw.context.work_height);
    place_root(raw, window, slots, state);
    state.decoration_eligible = decoration(window, state.applied_state);
    policy.windows.emplace(id, state);
  }
  for (const auto id : transient_parent_first(raw))
    place_transient(raw, id, policy);
}

}  // namespace glasswyrm::wm::detail
