#include "glasswyrmd/resource_table.hpp"

#include <algorithm>
#include <limits>
#include <new>

namespace glasswyrm::server {

bool ResourceTable::is_policy_candidate(const std::uint32_t xid) const noexcept {
  const auto* window = find_window(xid);
  return window && window->parent == screen_.root_window &&
         window->window_class == WindowClass::InputOutput;
}

void ResourceTable::recompute_map_states_from(const std::uint32_t xid,
                                              const bool parent_viewable) {
  auto* window = find_window(xid);
  if (!window) return;
  const bool candidate = is_policy_candidate(xid);
  if (!window->map_requested) window->map_state = MapState::Unmapped;
  else if (candidate && !window->policy_visible) window->map_state = MapState::Unmapped;
  else window->map_state = parent_viewable ? MapState::Viewable : MapState::Unviewable;
  const bool viewable = window->map_state == MapState::Viewable;
  const auto children = window->children;
  for (const auto child : children) recompute_map_states_from(child, viewable);
}

void ResourceTable::recompute_map_states(const std::uint32_t xid) {
  if (xid == screen_.root_window) {
    auto* root = find_window(xid); root->map_state = MapState::Viewable;
    const auto children = root->children;
    for (const auto child : children) recompute_map_states_from(child, true);
    return;
  }
  auto* window = find_window(xid);
  if (!window) return;
  const auto* parent = find_window(window->parent);
  recompute_map_states_from(xid, parent && parent->map_state == MapState::Viewable);
}

LocalLifecycleStatus ResourceTable::set_local_map_intent(const std::uint32_t xid,
                                                         const bool mapped) {
  auto* window = find_window(xid);
  if (!window) return LocalLifecycleStatus::BadWindow;
  if (xid == screen_.root_window || is_policy_candidate(xid))
    return LocalLifecycleStatus::BadMatch;
  window->map_requested = mapped;
  recompute_map_states(xid);
  return LocalLifecycleStatus::Success;
}

LocalLifecycleStatus ResourceTable::configure_local(
    const std::uint32_t xid, const LocalConfigure& configure) {
  auto* window = find_window(xid);
  if (!window) return LocalLifecycleStatus::BadWindow;
  if (xid == screen_.root_window || is_policy_candidate(xid))
    return LocalLifecycleStatus::BadMatch;
  if ((configure.width && (*configure.width == 0 || *configure.width > UINT16_MAX)) ||
      (configure.height && (*configure.height == 0 || *configure.height > UINT16_MAX)) ||
      (configure.border_width && *configure.border_width > UINT16_MAX) ||
      (configure.x && (*configure.x < INT16_MIN || *configure.x > INT16_MAX)) ||
      (configure.y && (*configure.y < INT16_MIN || *configure.y > INT16_MAX)))
    return LocalLifecycleStatus::BadValue;
  auto* parent = find_window(window->parent);
  if (!parent) return LocalLifecycleStatus::BadWindow;
  if (configure.sibling) {
    if (*configure.sibling == xid) return LocalLifecycleStatus::BadMatch;
    const auto* sibling = find_window(*configure.sibling);
    if (!sibling || sibling->parent != window->parent ||
        configure.stack_mode == LifecycleStackMode::None)
      return LocalLifecycleStatus::BadMatch;
  }
  if (configure.x) window->x = static_cast<std::int16_t>(*configure.x);
  if (configure.y) window->y = static_cast<std::int16_t>(*configure.y);
  if (configure.width) window->width = static_cast<std::uint16_t>(*configure.width);
  if (configure.height) window->height = static_cast<std::uint16_t>(*configure.height);
  if (configure.border_width)
    window->border_width = static_cast<std::uint16_t>(*configure.border_width);
  window->requested_x = window->x; window->requested_y = window->y;
  window->requested_width = window->width; window->requested_height = window->height;
  window->requested_border_width = window->border_width;
  if (configure.stack_mode != LifecycleStackMode::None) {
    std::erase(parent->children, xid);
    if (!configure.sibling) {
      if (configure.stack_mode == LifecycleStackMode::Above) parent->children.push_back(xid);
      else parent->children.insert(parent->children.begin(), xid);
    } else {
      auto position = std::find(parent->children.begin(), parent->children.end(),
                                *configure.sibling);
      if (configure.stack_mode == LifecycleStackMode::Above) ++position;
      parent->children.insert(position, xid);
    }
  }
  return LocalLifecycleStatus::Success;
}

bool ResourceTable::reorder_root_children(
    const std::vector<std::uint32_t>& visible_bottom_to_top) {
  auto* root = find_window(screen_.root_window);
  if (!root) return false;
  std::vector<std::uint32_t> result;
  result.reserve(root->children.size());
  for (const auto child : root->children)
    if (std::find(visible_bottom_to_top.begin(), visible_bottom_to_top.end(), child) ==
        visible_bottom_to_top.end()) result.push_back(child);
  for (const auto child : visible_bottom_to_top) {
    if (!is_policy_candidate(child) ||
        std::find(result.begin(), result.end(), child) != result.end()) return false;
    result.push_back(child);
  }
  root->children = std::move(result);
  return true;
}

CreateWindowStatus ResourceTable::create_window(
    const ClientId owner, const std::uint32_t resource_base,
    const std::uint32_t resource_mask, const WindowCreateSpec& spec) {
  if (!valid_new_resource_id(spec.xid, resource_base, resource_mask)) {
    return CreateWindowStatus::BadIdChoice;
  }
  auto* parent = find_window(spec.parent);
  if (parent == nullptr) {
    return CreateWindowStatus::BadWindow;
  }
  if (spec.width == 0 || spec.height == 0) {
    return CreateWindowStatus::BadValue;
  }

  WindowResource window;
  window.parent = spec.parent;
  window.x = spec.x;
  window.y = spec.y;
  window.width = spec.width;
  window.height = spec.height;
  window.border_width = spec.border_width;
  window.requested_x = spec.x;
  window.requested_y = spec.y;
  window.requested_width = spec.width;
  window.requested_height = spec.height;
  window.requested_border_width = spec.border_width;
  window.attributes = spec.attributes;
  if (spec.initial_event_mask != 0) {
    window.event_selections.emplace(owner, spec.initial_event_mask);
  }

  if (spec.window_class == WindowClass::CopyFromParent) {
    window.window_class = parent->window_class;
  } else if (spec.window_class == WindowClass::InputOutput ||
             spec.window_class == WindowClass::InputOnly) {
    window.window_class = spec.window_class;
  } else {
    return CreateWindowStatus::BadValue;
  }

  if (window.window_class == WindowClass::InputOnly) {
    constexpr std::uint32_t kInputOnlyAttributes =
        (1U << 5U) | (1U << 9U) | (1U << 11U) | (1U << 12U) | (1U << 14U);
    if (spec.depth != 0 || spec.visual != 0 || spec.border_width != 0 ||
        (spec.attribute_mask & ~kInputOnlyAttributes) != 0) {
      return CreateWindowStatus::BadMatch;
    }
    window.depth = 0;
    window.visual = 0;
  } else {
    if (parent->window_class == WindowClass::InputOnly) {
      return CreateWindowStatus::BadMatch;
    }
    window.depth = spec.depth == 0 ? parent->depth : spec.depth;
    window.visual = spec.visual == 0 ? parent->visual : spec.visual;
    if (window.depth != screen_.root_depth ||
        window.visual != screen_.root_visual) {
      return CreateWindowStatus::BadMatch;
    }
  }

  try {
    resources_.emplace(
        spec.xid,
        ResourceRecord{ResourceType::Window, owner, std::move(window)});
    try {
      resources_by_owner_[owner].push_back(spec.xid);
      find_window(spec.parent)->children.push_back(spec.xid);
    } catch (...) {
      auto owner_iterator = resources_by_owner_.find(owner);
      if (owner_iterator != resources_by_owner_.end()) {
        std::erase(owner_iterator->second, spec.xid);
        if (owner_iterator->second.empty()) {
          resources_by_owner_.erase(owner_iterator);
        }
      }
      resources_.erase(spec.xid);
      throw;
    }
  } catch (const std::bad_alloc&) {
    return CreateWindowStatus::BadAlloc;
  }
  return CreateWindowStatus::Success;
}

bool ResourceTable::set_event_selection(const std::uint32_t window_id,
                                        const ClientId client,
                                        const std::uint32_t mask) {
  auto* window = find_window(window_id);
  if (window == nullptr) {
    return false;
  }
  if (mask == 0) {
    window->event_selections.erase(client);
  } else {
    window->event_selections.insert_or_assign(client, mask);
  }
  return true;
}

std::uint32_t ResourceTable::event_selection(
    const std::uint32_t window_id, const ClientId client) const noexcept {
  const auto* window = find_window(window_id);
  if (window == nullptr) {
    return 0;
  }
  const auto iterator = window->event_selections.find(client);
  return iterator == window->event_selections.end() ? 0 : iterator->second;
}

std::uint32_t ResourceTable::all_event_selections(
    const std::uint32_t window_id) const noexcept {
  const auto* window = find_window(window_id);
  if (window == nullptr) {
    return 0;
  }
  std::uint32_t result = 0;
  for (const auto& [client, mask] : window->event_selections) {
    static_cast<void>(client);
    result |= mask;
  }
  return result;
}

void ResourceTable::remove_event_selections(const ClientId client) noexcept {
  for (auto& [xid, record] : resources_) {
    static_cast<void>(xid);
    if (auto* window = std::get_if<WindowResource>(&record.payload)) {
      window->event_selections.erase(client);
    }
  }
}

}  // namespace glasswyrm::server
