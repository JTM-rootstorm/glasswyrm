#include "glasswyrmd/resource_table.hpp"
#include "protocol/x11/event_mask.hpp"

#include <algorithm>
#include <unordered_set>

namespace glasswyrm::server {
namespace {

std::size_t window_property_bytes(const WindowResource& window) noexcept {
  std::size_t result = 0;
  for (const auto& [atom, property] : window.properties) {
    static_cast<void>(atom);
    result += property.byte_size();
  }
  return result;
}

}  // namespace

DestroyWindowStatus ResourceTable::destroy_window(const std::uint32_t xid,
                                                  CleanupResult* result) {
  if (xid == screen_.root_window) {
    return DestroyWindowStatus::RootPreserved;
  }
  if (find_window(xid) == nullptr) {
    return DestroyWindowStatus::BadWindow;
  }
  const auto plan = capture_destroy_plan(xid);
  if (!plan) return DestroyWindowStatus::BadWindow;
  return commit_destroy_plan(*plan, result);
}

std::optional<WindowDestroyPlan> ResourceTable::capture_destroy_plan(
    const std::uint32_t xid) const {
  constexpr auto kStructureNotifyMask =
      gw::protocol::x11::event_mask::StructureNotify;
  constexpr auto kSubstructureNotifyMask =
      gw::protocol::x11::event_mask::SubstructureNotify;
  if (xid == screen_.root_window || !find_window(xid)) return std::nullopt;
  WindowDestroyPlan plan;
  plan.root = xid;
  std::vector<std::pair<std::uint32_t, bool>> stack{{xid, false}};
  while (!stack.empty()) {
    const auto [current, expanded] = stack.back();
    stack.pop_back();
    const auto* window = find_window(current);
    if (!window) return std::nullopt;
    if (!expanded) {
      stack.emplace_back(current, true);
      for (const auto child : window->children)
        stack.emplace_back(child, false);
      continue;
    }
    ClientCleanupWindow entry{current, window->parent, {}, {}, std::nullopt};
    entry.owner = find(current)->owner;
    for (const auto& [client, mask] : window->event_selections)
      if ((mask & kStructureNotifyMask) != 0)
        entry.structure_recipients.push_back(client);
    if (const auto* parent = find_window(window->parent))
      for (const auto& [client, mask] : parent->event_selections)
        if ((mask & kSubstructureNotifyMask) != 0)
          entry.substructure_recipients.push_back(client);
    std::sort(entry.structure_recipients.begin(),
              entry.structure_recipients.end());
    std::sort(entry.substructure_recipients.begin(),
              entry.substructure_recipients.end());
    plan.postorder.push_back(std::move(entry));
  }
  return plan;
}

DestroyWindowStatus ResourceTable::commit_destroy_plan(
    const WindowDestroyPlan& plan, CleanupResult* result) {
  const auto current = capture_destroy_plan(plan.root);
  if (!current || current->postorder.size() != plan.postorder.size())
    return DestroyWindowStatus::BadWindow;
  for (std::size_t index = 0; index < plan.postorder.size(); ++index)
    if (current->postorder[index].xid != plan.postorder[index].xid ||
        current->postorder[index].parent != plan.postorder[index].parent)
      return DestroyWindowStatus::BadWindow;
  CleanupResult local;
  for (const auto& entry : plan.postorder) destroy_leaf(entry.xid, local);
  if (result != nullptr) {
    result->resources_destroyed += local.resources_destroyed;
    result->property_bytes_released += local.property_bytes_released;
  }
  return DestroyWindowStatus::Success;
}

void ResourceTable::destroy_leaf(const std::uint32_t xid,
                                 CleanupResult& result) {
  auto* window = find_window(xid);
  if (window == nullptr || xid == screen_.root_window) {
    return;
  }
  const std::uint32_t parent_id = window->parent;
  const std::size_t property_bytes = window_property_bytes(*window);
  const auto owner = find(xid)->owner;
  if (auto* parent = find_window(parent_id); parent != nullptr) {
    if (!parent->children.empty() && parent->children.back() == xid) {
      parent->children.pop_back();
    } else {
      const auto child =
          std::find(parent->children.begin(), parent->children.end(), xid);
      if (child != parent->children.end()) {
        parent->children.erase(child);
      }
    }
  }
  if (owner) {
    auto owner_iterator = resources_by_owner_.find(*owner);
    if (owner_iterator != resources_by_owner_.end()) {
      std::erase(owner_iterator->second, xid);
      if (owner_iterator->second.empty()) {
        resources_by_owner_.erase(owner_iterator);
      }
    }
  }
  resources_.erase(xid);
  total_property_bytes_ -= property_bytes;
  ++result.resources_destroyed;
  result.property_bytes_released += property_bytes;
}

CleanupResult ResourceTable::cleanup_client(const ClientId owner) {
  const auto plan = prepare_client_cleanup(owner);
  return commit_client_cleanup(plan);
}

ClientCleanupPlan ResourceTable::prepare_client_cleanup(const ClientId owner) {
  constexpr auto kStructureNotifyMask =
      gw::protocol::x11::event_mask::StructureNotify;
  constexpr auto kSubstructureNotifyMask =
      gw::protocol::x11::event_mask::SubstructureNotify;
  remove_event_selections(owner);
  ClientCleanupPlan plan;
  plan.owner = owner;
  const auto owned = resources_by_owner_.find(owner);
  if (owned == resources_by_owner_.end()) return plan;

  for (auto iterator = owned->second.rbegin(); iterator != owned->second.rend();
       ++iterator) {
    const auto xid = *iterator;
    const auto* window = find_window(xid);
    if (!window) continue;
    bool covered = false;
    auto parent = window->parent;
    while (parent != 0 && parent != screen_.root_window) {
      const auto* record = find(parent);
      if (!record) break;
      if (record->owner == owner) {
        covered = true;
        break;
      }
      const auto* parent_window = find_window(parent);
      if (!parent_window) break;
      parent = parent_window->parent;
    }
    if (!covered) plan.roots.push_back(xid);
  }

  std::unordered_set<std::uint32_t> captured;
  for (const auto root : plan.roots) {
    std::vector<std::pair<std::uint32_t, bool>> stack{{root, false}};
    while (!stack.empty()) {
      const auto [xid, expanded] = stack.back();
      stack.pop_back();
      auto* window = find_window(xid);
      if (!window) continue;
      if (expanded) {
        if (!captured.insert(xid).second) continue;
        window->cleanup_pending = true;
        plan.affects_policy = plan.affects_policy || is_policy_candidate(xid);
        ClientCleanupWindow captured_window{xid, window->parent, {}, {},
                                             std::nullopt};
        captured_window.owner = find(xid)->owner;
        for (const auto& [client, mask] : window->event_selections)
          if ((mask & kStructureNotifyMask) != 0)
            captured_window.structure_recipients.push_back(client);
        if (const auto* parent = find_window(window->parent))
          for (const auto& [client, mask] : parent->event_selections)
            if ((mask & kSubstructureNotifyMask) != 0)
              captured_window.substructure_recipients.push_back(client);
        std::sort(captured_window.structure_recipients.begin(),
                  captured_window.structure_recipients.end());
        std::sort(captured_window.substructure_recipients.begin(),
                  captured_window.substructure_recipients.end());
        plan.postorder.push_back(std::move(captured_window));
        continue;
      }
      stack.emplace_back(xid, true);
      for (const auto child : window->children)
        stack.emplace_back(child, false);
    }
  }
  return plan;
}

CleanupResult ResourceTable::commit_client_cleanup(
    const ClientCleanupPlan& plan) {
  CleanupResult result;
  for (const auto root : plan.roots) {
    const auto destroy = capture_destroy_plan(root);
    if (destroy) (void)commit_destroy_plan(*destroy, &result);
  }
  const auto owned = resources_by_owner_.find(plan.owner);
  if (owned != resources_by_owner_.end()) {
    const auto remaining = owned->second;
    for (const auto xid : remaining) {
      if (find_pixmap(xid)) (void)free_pixmap(xid);
      else if (find_gc(xid)) (void)free_gc(xid);
      else if (find_font(xid)) (void)close_font(xid);
      else if (find_cursor(xid)) (void)free_cursor(xid);
      ++result.resources_destroyed;
    }
  }
  return result;
}

bool ResourceTable::cleanup_pending(const std::uint32_t xid) const noexcept {
  const auto* window = find_window(xid);
  return window && window->cleanup_pending;
}

}  // namespace glasswyrm::server
