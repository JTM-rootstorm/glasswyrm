#include "glasswyrmd/lifecycle_snapshot.hpp"

#include <algorithm>
#include <limits>
#include <set>

namespace glasswyrm::server {

LifecycleSnapshot build_lifecycle_snapshot(const ResourceTable& resources,
                                           const std::uint32_t focused_window) {
  LifecycleSnapshot snapshot;
  snapshot.focused_window = focused_window;
  snapshot.root_window = resources.screen().root_window;
  const auto* root = resources.find_window(resources.screen().root_window);
  if (!root) return snapshot;
  snapshot.root_order = root->children;
  for (const auto xid : root->children) {
    const auto* window = resources.find_window(xid);
    if (!window || !resources.is_policy_candidate(xid)) continue;
    LifecycleWindow item;
    item.xid = xid;
    item.parent = window->parent;
    item.window_class = window->window_class;
    item.requested_x = window->requested_x;
    item.requested_y = window->requested_y;
    item.requested_width = window->requested_width;
    item.requested_height = window->requested_height;
    item.requested_border_width = window->requested_border_width;
    item.override_redirect = window->attributes.override_redirect;
    item.map_requested = window->map_requested;
    item.policy_visible = window->policy_visible;
    item.focused = window->focused;
    item.stacking = window->stacking;
    item.creation_serial = window->creation_serial;
    item.map_serial = window->map_serial;
    item.focus_serial = window->focus_serial;
    item.geometry_serial = window->geometry_serial;
    item.stack_serial = window->stack_serial;
    item.stack_sibling = window->stack_sibling;
    item.stack_mode = window->stack_mode;
    item.transient_for = window->transient_for;
    item.policy_window_type = window->policy_window_type;
    item.decoration_preference = window->decoration_preference;
    item.fullscreen_requested = window->fullscreen_requested;
    item.maximized_requested = window->maximized_requested;
    item.above_requested = window->above_requested;
    item.bypass_compositor = window->bypass_compositor;
    item.attention_requested = window->attention_requested;
    item.input_requested = window->input_requested;
    item.minimum_width = window->minimum_width;
    item.minimum_height = window->minimum_height;
    item.maximum_width = window->maximum_width;
    item.maximum_height = window->maximum_height;
    item.saved_normal_geometry = window->saved_normal_geometry;
    item.applied_x = window->x;
    item.applied_y = window->y;
    item.applied_width = window->width;
    item.applied_height = window->height;
    item.window_type = static_cast<std::uint8_t>(window->policy_window_type);
    item.applied_state = 1;
    item.managed = !window->attributes.override_redirect;
    item.decoration_eligible = !window->attributes.override_redirect;
    item.attention_requested = window->attention_requested;
    item.fullscreen_eligible = 1;
    snapshot.windows.emplace(xid, std::move(item));
  }
  return snapshot;
}

bool apply_policy_state(ResourceTable& resources,
                        const std::span<const AppliedPolicyWindow> policy,
                        std::uint32_t& focused_window) {
  std::set<std::uint32_t> ids;
  std::set<std::int32_t> stacks;
  std::vector<std::uint32_t> visible;
  std::uint32_t focus = resources.screen().root_window;
  for (const auto& item : policy) {
    const auto* window = resources.find_window(item.xid);
    if (!window || !resources.is_policy_candidate(item.xid) ||
        !ids.insert(item.xid).second || item.width == 0 || item.height == 0 ||
        item.width > UINT16_MAX || item.height > UINT16_MAX ||
        item.x < INT16_MIN || item.x > INT16_MAX || item.y < INT16_MIN ||
        item.y > INT16_MAX || (item.visible && item.stacking < 0) ||
        (!item.visible && item.stacking != -1) ||
        (item.visible && !stacks.insert(item.stacking).second)) return false;
    if (item.focused) {
      if (!item.visible || focus != resources.screen().root_window) return false;
      focus = item.xid;
    }
  }
  if (ids.size() != build_lifecycle_snapshot(resources, focused_window).windows.size())
    return false;
  for (std::size_t index = 0; index < stacks.size(); ++index)
    if (!stacks.contains(static_cast<std::int32_t>(index))) return false;
  auto sorted = std::vector<AppliedPolicyWindow>(policy.begin(), policy.end());
  std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
    return a.stacking < b.stacking;
  });
  for (const auto& item : sorted) if (item.visible) visible.push_back(item.xid);
  if (!resources.reorder_root_children(visible)) return false;
  for (const auto& item : policy) {
    auto* window = resources.find_window(item.xid);
    window->x = static_cast<std::int16_t>(item.x);
    window->y = static_cast<std::int16_t>(item.y);
    window->width = static_cast<std::uint16_t>(item.width);
    window->height = static_cast<std::uint16_t>(item.height);
    window->policy_visible = item.visible;
    window->focused = item.focused;
    window->stacking = item.stacking;
    if (window->map_requested && window->geometry_serial == 0) {
      window->requested_x = item.x; window->requested_y = item.y;
      window->requested_width = item.width; window->requested_height = item.height;
    }
  }
  resources.recompute_map_states(resources.screen().root_window);
  focused_window = focus;
  return true;
}

}  // namespace glasswyrm::server
