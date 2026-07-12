#include "glasswyrmd/resource_table.hpp"

#include "glasswyrmd/resource_id.hpp"

#include <algorithm>
#include <limits>
#include <new>
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

ResourceTable::ResourceTable(const ScreenModel screen, ResourceLimits limits)
    : screen_(screen), limits_(limits) {
  WindowResource root;
  root.width = screen.width_pixels;
  root.height = screen.height_pixels;
  root.depth = screen.root_depth;
  root.window_class = WindowClass::InputOutput;
  root.visual = screen.root_visual;
  root.map_state = MapState::Viewable;
  root.attributes.colormap = screen.default_colormap;
  resources_.emplace(
      screen.root_window,
      ResourceRecord{ResourceType::Window, std::nullopt, std::move(root)});
}

const ResourceRecord* ResourceTable::find(const std::uint32_t xid) const noexcept {
  const auto iterator = resources_.find(xid);
  return iterator == resources_.end() ? nullptr : &iterator->second;
}

ResourceRecord* ResourceTable::find(const std::uint32_t xid) noexcept {
  const auto iterator = resources_.find(xid);
  return iterator == resources_.end() ? nullptr : &iterator->second;
}

const WindowResource* ResourceTable::find_window(
    const std::uint32_t xid) const noexcept {
  const auto* resource = find(xid);
  return resource == nullptr ? nullptr
                             : std::get_if<WindowResource>(&resource->payload);
}

WindowResource* ResourceTable::find_window(const std::uint32_t xid) noexcept {
  auto* resource = find(xid);
  return resource == nullptr ? nullptr
                             : std::get_if<WindowResource>(&resource->payload);
}

const PixmapResource* ResourceTable::find_pixmap(const std::uint32_t xid) const noexcept {
  const auto* resource = find(xid);
  return resource ? std::get_if<PixmapResource>(&resource->payload) : nullptr;
}
PixmapResource* ResourceTable::find_pixmap(const std::uint32_t xid) noexcept {
  auto* resource = find(xid);
  return resource ? std::get_if<PixmapResource>(&resource->payload) : nullptr;
}
const GraphicsContextResource* ResourceTable::find_gc(const std::uint32_t xid) const noexcept {
  const auto* resource = find(xid);
  return resource ? std::get_if<GraphicsContextResource>(&resource->payload) : nullptr;
}
GraphicsContextResource* ResourceTable::find_gc(const std::uint32_t xid) noexcept {
  auto* resource = find(xid);
  return resource ? std::get_if<GraphicsContextResource>(&resource->payload) : nullptr;
}

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

bool ResourceTable::valid_new_resource_id(
    const std::uint32_t xid, const std::uint32_t resource_base,
    const std::uint32_t resource_mask) const {
  return resource_id_matches_client(xid, resource_base, resource_mask) &&
         !is_server_owned_id(xid, screen_) && !resources_.contains(xid);
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

CreatePixmapStatus ResourceTable::create_pixmap(
    const ClientId owner, const std::uint32_t resource_base,
    const std::uint32_t resource_mask, const std::uint32_t xid,
    const std::uint32_t drawable, const std::uint8_t depth,
    const std::uint16_t width, const std::uint16_t height) {
  if (!valid_new_resource_id(xid, resource_base, resource_mask))
    return CreatePixmapStatus::BadIdChoice;
  const auto* anchor_window = find_window(drawable);
  const auto valid_anchor = drawable == screen_.root_window || anchor_window || find_pixmap(drawable);
  if (!valid_anchor) return CreatePixmapStatus::BadDrawable;
  if (anchor_window && drawable != screen_.root_window &&
      (anchor_window->parent != screen_.root_window ||
       anchor_window->window_class != WindowClass::InputOutput || anchor_window->depth != 24))
    return CreatePixmapStatus::BadMatch;
  if (depth != 24 || width == 0 || height == 0)
    return CreatePixmapStatus::BadValue;
  if (resource_count(ResourceType::Pixmap) >= limits_.maximum_pixmaps)
    return CreatePixmapStatus::BadAlloc;
  auto storage = PixelStorage::create(width, height);
  if (!storage) return CreatePixmapStatus::BadAlloc;
  if (storage->byte_size() > limits_.maximum_canonical_drawable_bytes -
                                 canonical_drawable_bytes_)
    return CreatePixmapStatus::BadAlloc;
  const auto bytes = storage->byte_size();
  try {
    PixmapResource pixmap{screen_.root_window, depth, width, height,
                          std::make_shared<PixelStorage>(std::move(*storage))};
    resources_.emplace(xid, ResourceRecord{ResourceType::Pixmap, owner,
                                           std::move(pixmap)});
    try { resources_by_owner_[owner].push_back(xid); }
    catch (...) { resources_.erase(xid); throw; }
    canonical_drawable_bytes_ += bytes;
  } catch (const std::bad_alloc&) { return CreatePixmapStatus::BadAlloc; }
  return CreatePixmapStatus::Success;
}

FreePixmapStatus ResourceTable::free_pixmap(const std::uint32_t xid) {
  const auto* pixmap = find_pixmap(xid);
  if (!pixmap) return FreePixmapStatus::BadPixmap;
  const auto owner = find(xid)->owner;
  const auto bytes = pixmap->storage ? pixmap->storage->byte_size() : 0;
  resources_.erase(xid);
  if (owner) {
    auto iterator = resources_by_owner_.find(*owner);
    if (iterator != resources_by_owner_.end()) {
      std::erase(iterator->second, xid);
      if (iterator->second.empty()) resources_by_owner_.erase(iterator);
    }
  }
  canonical_drawable_bytes_ -= bytes;
  return FreePixmapStatus::Success;
}

CreateGcStatus ResourceTable::create_gc(
    const ClientId owner, const std::uint32_t resource_base,
    const std::uint32_t resource_mask, const std::uint32_t xid,
    const std::uint32_t drawable, GraphicsContextResource gc) {
  if (!valid_new_resource_id(xid, resource_base, resource_mask))
    return CreateGcStatus::BadIdChoice;
  std::uint8_t depth = 0;
  if (drawable == screen_.root_window) depth = screen_.root_depth;
  else if (const auto* window = find_window(drawable)) {
    if (window->parent != screen_.root_window ||
        window->window_class != WindowClass::InputOutput)
      return CreateGcStatus::BadMatch;
    depth = window->depth;
  }
  else if (const auto* pixmap = find_pixmap(drawable)) depth = pixmap->depth;
  else return CreateGcStatus::BadDrawable;
  if (depth != 24) return CreateGcStatus::BadMatch;
  if (resource_count(ResourceType::GraphicsContext) >=
      limits_.maximum_graphics_contexts)
    return CreateGcStatus::BadAlloc;
  gc.root = screen_.root_window; gc.depth = depth;
  try {
    resources_.emplace(xid, ResourceRecord{ResourceType::GraphicsContext, owner,
                                           std::move(gc)});
    try { resources_by_owner_[owner].push_back(xid); }
    catch (...) { resources_.erase(xid); throw; }
  } catch (const std::bad_alloc&) { return CreateGcStatus::BadAlloc; }
  return CreateGcStatus::Success;
}

FreeGcStatus ResourceTable::free_gc(const std::uint32_t xid) {
  if (!find_gc(xid)) return FreeGcStatus::BadGContext;
  const auto owner = find(xid)->owner;
  resources_.erase(xid);
  if (owner) {
    auto iterator = resources_by_owner_.find(*owner);
    if (iterator != resources_by_owner_.end()) {
      std::erase(iterator->second, xid);
      if (iterator->second.empty()) resources_by_owner_.erase(iterator);
    }
  }
  return FreeGcStatus::Success;
}

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
  constexpr std::uint32_t kStructureNotifyMask = 1U << 17U;
  constexpr std::uint32_t kSubstructureNotifyMask = 1U << 19U;
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
  constexpr std::uint32_t kStructureNotifyMask = 1U << 17U;
  constexpr std::uint32_t kSubstructureNotifyMask = 1U << 19U;
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
      ++result.resources_destroyed;
    }
  }
  return result;
}

bool ResourceTable::cleanup_pending(const std::uint32_t xid) const noexcept {
  const auto* window = find_window(xid);
  return window && window->cleanup_pending;
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

PropertyMutationStatus ResourceTable::change_property(
    const std::uint32_t window_id, const std::uint32_t property_atom,
    Property value, const PropertyMode mode) {
  auto* window = find_window(window_id);
  if (window == nullptr) {
    return PropertyMutationStatus::BadWindow;
  }
  auto current = window->properties.find(property_atom);
  const bool exists = current != window->properties.end();
  if (exists && mode != PropertyMode::Replace &&
      (current->second.type != value.type ||
       current->second.format() != value.format())) {
    return PropertyMutationStatus::BadMatch;
  }

  try {
    Property replacement = std::move(value);
    if (exists && mode == PropertyMode::Append) {
      replacement.data = concatenate_property_data(current->second.data,
                                                   replacement.data);
    } else if (exists && mode == PropertyMode::Prepend) {
      replacement.data = concatenate_property_data(replacement.data,
                                                   current->second.data);
    }
    const std::size_t old_size = exists ? current->second.byte_size() : 0;
    const std::size_t new_size = replacement.byte_size();
    if (new_size > limits_.maximum_bytes_per_property ||
        (!exists && window->properties.size() >=
                        limits_.maximum_properties_per_window) ||
        new_size > limits_.maximum_total_property_bytes -
                       (total_property_bytes_ - old_size)) {
      return PropertyMutationStatus::BadAlloc;
    }

    if (exists) {
      current->second = std::move(replacement);
    } else {
      window->properties.emplace(property_atom, std::move(replacement));
    }
    total_property_bytes_ = total_property_bytes_ - old_size + new_size;
  } catch (const std::bad_alloc&) {
    return PropertyMutationStatus::BadAlloc;
  }
  return PropertyMutationStatus::Success;
}

bool ResourceTable::delete_property(const std::uint32_t window_id,
                                    const std::uint32_t property_atom) {
  auto* window = find_window(window_id);
  if (window == nullptr) {
    return false;
  }
  const auto iterator = window->properties.find(property_atom);
  if (iterator == window->properties.end()) {
    return true;
  }
  total_property_bytes_ -= iterator->second.byte_size();
  window->properties.erase(iterator);
  return true;
}

PropertyReadResult ResourceTable::get_property(
    const std::uint32_t window_id, const std::uint32_t property_atom,
    const std::uint32_t requested_type, const bool delete_after_read,
    const std::uint32_t long_offset, const std::uint32_t long_length) {
  auto* window = find_window(window_id);
  if (window == nullptr) {
    PropertyReadResult result;
    result.status = PropertyReadStatus::BadWindow;
    return result;
  }
  const auto iterator = window->properties.find(property_atom);
  if (iterator == window->properties.end()) {
    return {};
  }
  const auto& property = iterator->second;
  const std::size_t byte_size = property.byte_size();
  const std::uint64_t offset64 = static_cast<std::uint64_t>(long_offset) * 4U;
  if (offset64 > byte_size) {
    PropertyReadResult result;
    result.status = PropertyReadStatus::BadValue;
    return result;
  }

  PropertyReadResult result;
  result.present = true;
  result.value.type = property.type;
  result.value.format = property.format();
  result.type_matched = requested_type == 0 || requested_type == property.type;
  if (!result.type_matched) {
    result.value.bytes_after = static_cast<std::uint32_t>(byte_size);
    return result;
  }

  const std::size_t offset = static_cast<std::size_t>(offset64);
  const std::uint64_t requested64 = static_cast<std::uint64_t>(long_length) * 4U;
  const std::size_t available = byte_size - offset;
  const std::size_t returned = static_cast<std::size_t>(
      std::min<std::uint64_t>(available, requested64));
  result.value.bytes_after = static_cast<std::uint32_t>(available - returned);
  result.value.data = slice_property_data(property.data, offset, returned);
  if (delete_after_read && result.value.bytes_after == 0) {
    total_property_bytes_ -= byte_size;
    window->properties.erase(iterator);
    result.deleted = true;
  }
  return result;
}

std::vector<std::uint32_t> ResourceTable::list_properties(
    const std::uint32_t window_id) const {
  std::vector<std::uint32_t> result;
  const auto* window = find_window(window_id);
  if (window == nullptr) {
    return result;
  }
  result.reserve(window->properties.size());
  for (const auto& [atom, property] : window->properties) {
    static_cast<void>(property);
    result.push_back(atom);
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::size_t ResourceTable::resource_count(const ResourceType type) const noexcept {
  return static_cast<std::size_t>(std::count_if(
      resources_.begin(), resources_.end(),
      [type](const auto& entry) { return entry.second.type == type; }));
}

std::size_t ResourceTable::resource_count_by_owner(
    const ClientId owner) const noexcept {
  const auto iterator = resources_by_owner_.find(owner);
  return iterator == resources_by_owner_.end() ? 0 : iterator->second.size();
}

bool ResourceTable::invariants_hold() const noexcept {
  const auto* root = find_window(screen_.root_window);
  if (root == nullptr || root->parent != 0 || !find(screen_.root_window) ||
      find(screen_.root_window)->owner.has_value()) {
    return false;
  }
  std::size_t calculated_property_bytes = 0;
  std::size_t calculated_drawable_bytes = 0;
  for (const auto& [xid, resource] : resources_) {
    const auto* window = std::get_if<WindowResource>(&resource.payload);
    if (window == nullptr) {
      if (!resource.owner || resource.type == ResourceType::Window) return false;
      const auto owner_iterator = resources_by_owner_.find(*resource.owner);
      if (owner_iterator == resources_by_owner_.end() ||
          std::count(owner_iterator->second.begin(), owner_iterator->second.end(), xid) != 1)
        return false;
      if (const auto* pixmap = std::get_if<PixmapResource>(&resource.payload))
        calculated_drawable_bytes += pixmap->storage ? pixmap->storage->byte_size() : 0;
      continue;
    }
    calculated_property_bytes += window_property_bytes(*window);
    if (xid != screen_.root_window) {
      const auto* parent = find_window(window->parent);
      if (parent == nullptr ||
          std::count(parent->children.begin(), parent->children.end(), xid) != 1 ||
          !resource.owner) {
        return false;
      }
      const auto owner_iterator = resources_by_owner_.find(*resource.owner);
      if (owner_iterator == resources_by_owner_.end() ||
          std::count(owner_iterator->second.begin(), owner_iterator->second.end(),
                     xid) != 1) {
        return false;
      }
    }
    for (const auto child : window->children) {
      const auto* child_window = find_window(child);
      if (child_window == nullptr || child_window->parent != xid) {
        return false;
      }
    }
  }
  if (calculated_property_bytes != total_property_bytes_) {
    return false;
  }
  if (calculated_drawable_bytes != canonical_drawable_bytes_) return false;
  for (const auto& [owner, ids] : resources_by_owner_) {
    for (const auto xid : ids) {
      const auto* resource = find(xid);
      if (resource == nullptr || resource->owner != owner ||
          std::count(ids.begin(), ids.end(), xid) != 1) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace glasswyrm::server
