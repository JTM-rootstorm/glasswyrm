#include "wm/policy_engine.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace glasswyrm::wm {
namespace {

bool extent_fits(const std::int32_t origin, const std::uint32_t extent) {
  return extent != 0 &&
         static_cast<std::int64_t>(origin) + extent - 1 <=
             std::numeric_limits<std::int32_t>::max();
}

EvaluationError validate(const RawState& raw) {
  if (!raw.complete || !raw.has_context) return EvaluationError::IncompleteSnapshot;
  const auto& context = raw.context;
  if (context.root_window_id == 0 || context.workspace_id == 0 ||
      context.output_id == 0 || context.flags != 0 || context.work_width == 0 ||
      context.work_height == 0 || context.work_width > maximum_work_extent ||
      context.work_height > maximum_work_extent ||
      !extent_fits(context.work_x, context.work_width) ||
      !extent_fits(context.work_y, context.work_height))
    return EvaluationError::InvalidContext;
  if (raw.windows.size() > maximum_windows) return EvaluationError::Limit;

  std::set<std::uint64_t> creation_serials;
  for (const auto& [id, window] : raw.windows) {
    if (id == 0 || id != window.window_id || id == context.root_window_id ||
        window.parent_window_id != context.root_window_id ||
        window.creation_serial == 0 || window.requested_width == 0 ||
        window.requested_height == 0 ||
        window.requested_width > maximum_window_extent ||
        window.requested_height > maximum_window_extent ||
        window.border_width > maximum_border_width || window.flags != 0 ||
        (window.wants_map != (window.map_serial != 0)) ||
        !extent_fits(window.requested_x, window.requested_width) ||
        !extent_fits(window.requested_y, window.requested_height) ||
        !creation_serials.insert(window.creation_serial).second)
      return EvaluationError::InvalidWindow;
    if (window.workspace_id != 0 && window.workspace_id != context.workspace_id)
      return EvaluationError::UnsupportedMetadata;
    if (window.transient_for == id) return EvaluationError::InvalidWindow;
    if (window.transient_for != 0) {
      const auto target = raw.windows.find(window.transient_for);
      if (target == raw.windows.end() || target->second.override_redirect)
        return EvaluationError::UnknownReference;
    }
  }

  for (const auto& [id, window] : raw.windows) {
    (void)window;
    std::set<std::uint32_t> visited;
    auto current = id;
    while (current != 0) {
      if (!visited.insert(current).second) return EvaluationError::InvalidWindow;
      const auto found = raw.windows.find(current);
      if (found == raw.windows.end()) break;
      current = found->second.transient_for;
    }
  }
  return EvaluationError::None;
}

auto stack_key(const RawWindow& window) {
  return std::tuple(window.map_serial, window.creation_serial, window.window_id);
}

bool decoration(const RawWindow& raw, const AppliedState state) {
  if (raw.override_redirect || state == AppliedState::Fullscreen) return false;
  if (raw.decoration_preference == DecorationPreference::False) return false;
  if (raw.window_type == WindowType::Utility || raw.window_type == WindowType::Unknown)
    return raw.decoration_preference == DecorationPreference::True;
  return true;
}

AppliedState applied_state(const RawWindow& raw) {
  if (raw.override_redirect) return AppliedState::Normal;
  if (raw.minimized_requested) return AppliedState::Minimized;
  if (raw.fullscreen_requested) return AppliedState::Fullscreen;
  if (raw.maximized_requested) return AppliedState::Maximized;
  return AppliedState::Normal;
}

std::int32_t clamp_coordinate(const std::int64_t value, const std::int32_t low,
                              const std::int64_t high) {
  return static_cast<std::int32_t>(std::clamp(value, static_cast<std::int64_t>(low), high));
}

void hash_byte(std::uint64_t& hash, const std::uint8_t value) {
  hash ^= value;
  hash *= UINT64_C(1099511628211);
}
template <class Value>
void hash_little(std::uint64_t& hash, Value value) {
  using Unsigned = std::make_unsigned_t<Value>;
  auto bits = static_cast<Unsigned>(value);
  for (std::size_t index = 0; index < sizeof(Value); ++index) {
    hash_byte(hash, static_cast<std::uint8_t>(bits));
    bits >>= 8U;
  }
}

std::uint64_t policy_hash(const PolicyState& policy) {
  std::uint64_t hash = UINT64_C(14695981039346656037);
  constexpr std::string_view tag = "glasswyrm-policy-v1";
  for (const char byte : tag) hash_byte(hash, static_cast<std::uint8_t>(byte));
  hash_little(hash, policy.generation);
  const auto& context = policy.context;
  hash_little(hash, context.root_window_id); hash_little(hash, context.workspace_id);
  hash_little(hash, context.output_id); hash_little(hash, context.work_x);
  hash_little(hash, context.work_y); hash_little(hash, context.work_width);
  hash_little(hash, context.work_height); hash_little(hash, context.flags);
  for (const auto id : policy.output_order) {
    const auto& window = policy.windows.at(id);
    hash_little(hash, window.window_id); hash_little(hash, window.transient_for);
    hash_little(hash, window.workspace_id); hash_little(hash, UINT32_C(0));
    hash_little(hash, window.output_id);
    hash_little(hash, window.final_x); hash_little(hash, window.final_y);
    hash_little(hash, window.final_width); hash_little(hash, window.final_height);
    hash_little(hash, window.stacking);
    hash_little(hash, static_cast<std::uint16_t>(window.window_type));
    hash_little(hash, static_cast<std::uint16_t>(window.applied_state));
    hash_byte(hash, window.visible); hash_byte(hash, window.focused);
    hash_byte(hash, window.managed); hash_byte(hash, window.decoration_eligible);
    hash_byte(hash, window.override_redirect); hash_byte(hash, window.attention_requested);
    hash_byte(hash, static_cast<std::uint8_t>(window.fullscreen_eligible));
    hash_byte(hash, static_cast<std::uint8_t>(window.direct_scanout_eligible));
    hash_little(hash, UINT32_C(0)); hash_little(hash, UINT32_C(0));
  }
  return hash;
}

}  // namespace

Evaluation evaluate(const RawState& raw, const std::uint64_t generation) {
  Evaluation result;
  result.error = validate(raw);
  if (result.error != EvaluationError::None || generation == 0) {
    if (generation == 0 && result.error == EvaluationError::None)
      result.error = EvaluationError::InvalidWindow;
    return result;
  }
  auto& policy = result.policy;
  policy.generation = generation;
  policy.context = raw.context;

  std::vector<const RawWindow*> cascade;
  for (const auto& [id, window] : raw.windows) {
    (void)id;
    if (!window.override_redirect && window.transient_for == 0 && window.wants_map &&
        !window.minimized_requested && !window.fullscreen_requested &&
        !window.maximized_requested &&
        (window.window_type == WindowType::Normal ||
         window.window_type == WindowType::Utility ||
         window.window_type == WindowType::Unknown))
      cascade.push_back(&window);
  }
  std::sort(cascade.begin(), cascade.end(), [](const auto* left, const auto* right) {
    return std::tie(left->creation_serial, left->window_id) <
           std::tie(right->creation_serial, right->window_id);
  });
  std::map<std::uint32_t, std::size_t> cascade_slots;
  for (std::size_t index = 0; index < cascade.size(); ++index)
    cascade_slots.emplace(cascade[index]->window_id, index);

  for (const auto& [id, window] : raw.windows) {
    WindowState state;
    state.window_id = id; state.transient_for = window.transient_for;
    state.workspace_id = raw.context.workspace_id; state.output_id = raw.context.output_id;
    state.window_type = window.window_type; state.applied_state = applied_state(window);
    state.managed = !window.override_redirect; state.override_redirect = window.override_redirect;
    state.attention_requested = window.attention_requested;
    state.final_width = window.override_redirect
                            ? window.requested_width
                            : std::min(window.requested_width, raw.context.work_width);
    state.final_height = window.override_redirect
                             ? window.requested_height
                             : std::min(window.requested_height, raw.context.work_height);
    if (window.override_redirect) {
      state.final_x = window.requested_x; state.final_y = window.requested_y;
    } else if (state.applied_state == AppliedState::Fullscreen ||
               state.applied_state == AppliedState::Maximized) {
      state.final_x = raw.context.work_x; state.final_y = raw.context.work_y;
      state.final_width = raw.context.work_width; state.final_height = raw.context.work_height;
    } else if (window.transient_for == 0) {
      const auto slot = cascade_slots.contains(id) ? cascade_slots.at(id) : 0;
      const auto x_span = raw.context.work_width - state.final_width;
      const auto y_span = raw.context.work_height - state.final_height;
      state.final_x = raw.context.work_x + static_cast<std::int32_t>(
          x_span == 0 ? 0 : (slot * 32U) % (static_cast<std::size_t>(x_span) + 1U));
      state.final_y = raw.context.work_y + static_cast<std::int32_t>(
          y_span == 0 ? 0 : (slot * 32U) % (static_cast<std::size_t>(y_span) + 1U));
    }
    state.decoration_eligible = decoration(window, state.applied_state);
    state.fullscreen_eligible = window.override_redirect
                                    ? TriState::Unknown
                                    : (state.applied_state == AppliedState::Fullscreen
                                           ? TriState::True : TriState::False);
    state.visible = window.wants_map &&
                    (window.override_redirect || state.applied_state != AppliedState::Minimized);
    policy.windows.emplace(id, state);
  }

  // A transient's geometry and visibility depend on its parent, so resolve roots first.
  std::vector<std::uint32_t> unresolved;
  for (const auto& [id, window] : raw.windows)
    if (window.transient_for != 0) unresolved.push_back(id);
  const auto transient_depth = [&](const std::uint32_t id) {
    std::size_t depth = 0;
    auto current = raw.windows.at(id).transient_for;
    while (current != 0) {
      ++depth;
      current = raw.windows.at(current).transient_for;
    }
    return depth;
  };
  std::sort(unresolved.begin(), unresolved.end(), [&](const auto left, const auto right) {
    return std::tuple(transient_depth(left), left) <
           std::tuple(transient_depth(right), right);
  });
  for (const auto id : unresolved) {
    const auto& raw_window = raw.windows.at(id);
    auto& state = policy.windows.at(id);
    const auto& parent = policy.windows.at(raw_window.transient_for);
    const std::int64_t centered_x = static_cast<std::int64_t>(parent.final_x) +
        (static_cast<std::int64_t>(parent.final_width) - state.final_width) / 2;
    const std::int64_t centered_y = static_cast<std::int64_t>(parent.final_y) +
        (static_cast<std::int64_t>(parent.final_height) - state.final_height) / 2;
    state.final_x = clamp_coordinate(centered_x, raw.context.work_x,
        static_cast<std::int64_t>(raw.context.work_x) + raw.context.work_width - state.final_width);
    state.final_y = clamp_coordinate(centered_y, raw.context.work_y,
        static_cast<std::int64_t>(raw.context.work_y) + raw.context.work_height - state.final_height);
    state.visible = state.visible && parent.visible;
  }

  std::vector<std::uint32_t> managed_roots;
  std::vector<std::uint32_t> overrides;
  for (const auto& [id, window] : raw.windows) {
    if (!policy.windows.at(id).visible) continue;
    if (window.override_redirect) overrides.push_back(id);
    else if (window.transient_for == 0) managed_roots.push_back(id);
  }
  const auto sort_ids = [&](std::vector<std::uint32_t>& ids) {
    std::sort(ids.begin(), ids.end(), [&](const auto left, const auto right) {
      return stack_key(raw.windows.at(left)) < stack_key(raw.windows.at(right));
    });
  };
  sort_ids(managed_roots); sort_ids(overrides);
  std::vector<std::uint32_t> stacking;
  const auto emit = [&](const auto& self, const std::uint32_t id) -> void {
    stacking.push_back(id);
    std::vector<std::uint32_t> children;
    for (const auto& [child_id, child] : raw.windows)
      if (child.transient_for == id && policy.windows.at(child_id).visible)
        children.push_back(child_id);
    sort_ids(children);
    for (const auto child : children) self(self, child);
  };
  for (const auto id : managed_roots) emit(emit, id);
  stacking.insert(stacking.end(), overrides.begin(), overrides.end());
  for (std::size_t index = 0; index < stacking.size(); ++index)
    policy.windows.at(stacking[index]).stacking = static_cast<std::int32_t>(index);

  std::vector<std::uint32_t> focus;
  for (const auto& [id, window] : raw.windows)
    if (policy.windows.at(id).visible && !window.override_redirect) focus.push_back(id);
  if (!focus.empty()) {
    const bool has_explicit = std::any_of(focus.begin(), focus.end(), [&](const auto id) {
      return raw.windows.at(id).focus_serial != 0;
    });
    const auto selected = *std::max_element(focus.begin(), focus.end(), [&](const auto left,
                                                                            const auto right) {
      const auto& l = raw.windows.at(left); const auto& r = raw.windows.at(right);
      return has_explicit
          ? std::tie(l.focus_serial, l.map_serial, l.creation_serial, l.window_id) <
                std::tie(r.focus_serial, r.map_serial, r.creation_serial, r.window_id)
          : stack_key(l) < stack_key(r);
    });
    policy.windows.at(selected).focused = true;
  }

  policy.output_order = stacking;
  for (const auto& [id, state] : policy.windows)
    if (!state.visible) policy.output_order.push_back(id);
  policy.hash = policy_hash(policy);
  return result;
}

}  // namespace glasswyrm::wm
