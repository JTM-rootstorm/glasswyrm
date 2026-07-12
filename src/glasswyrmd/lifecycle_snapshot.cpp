#include "glasswyrmd/lifecycle_snapshot.hpp"

#include <algorithm>
#include <limits>
#include <set>

namespace glasswyrm::server {

LifecycleSnapshot build_lifecycle_snapshot(const ResourceTable& resources,
                                           const std::uint32_t focused_window) {
  LifecycleSnapshot snapshot;
  snapshot.focused_window = focused_window;
  const auto* root = resources.find_window(resources.screen().root_window);
  if (!root) return snapshot;
  snapshot.root_order = root->children;
  for (const auto xid : root->children) {
    const auto* window = resources.find_window(xid);
    if (!window || !resources.is_policy_candidate(xid)) continue;
    snapshot.windows.emplace(xid, LifecycleWindow{
        xid, window->parent, window->window_class, window->requested_x,
        window->requested_y, window->requested_width, window->requested_height,
        window->requested_border_width, window->attributes.override_redirect,
        window->map_requested, window->policy_visible, window->focused,
        window->creation_serial, window->map_serial, window->focus_serial,
        window->geometry_serial, window->stack_serial, window->stack_sibling,
        window->stack_mode});
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
