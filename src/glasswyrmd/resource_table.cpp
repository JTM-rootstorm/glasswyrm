#include "glasswyrmd/resource_table.hpp"

#include "glasswyrmd/resource_id.hpp"

#include <algorithm>
#include <limits>
#include <new>

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
  window.attributes = spec.attributes;

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

DestroyWindowStatus ResourceTable::destroy_window(const std::uint32_t xid,
                                                  CleanupResult* result) {
  if (xid == screen_.root_window) {
    return DestroyWindowStatus::RootPreserved;
  }
  if (find_window(xid) == nullptr) {
    return DestroyWindowStatus::BadWindow;
  }
  CleanupResult local;
  destroy_window_tree(xid, local);
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

void ResourceTable::destroy_window_tree(const std::uint32_t xid,
                                        CleanupResult& result) {
  std::uint32_t current = xid;
  while (true) {
    auto* window = find_window(current);
    if (window == nullptr || current == screen_.root_window) {
      return;
    }
    if (!window->children.empty()) {
      current = window->children.back();
      continue;
    }
    const std::uint32_t parent = window->parent;
    const bool finished = current == xid;
    destroy_leaf(current, result);
    if (finished) {
      return;
    }
    current = parent;
  }
}

CleanupResult ResourceTable::cleanup_client(const ClientId owner) {
  CleanupResult result;
  while (true) {
    const auto iterator = resources_by_owner_.find(owner);
    if (iterator == resources_by_owner_.end() || iterator->second.empty()) {
      break;
    }
    destroy_window_tree(iterator->second.back(), result);
  }
  return result;
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
  for (const auto& [xid, resource] : resources_) {
    const auto* window = std::get_if<WindowResource>(&resource.payload);
    if (window == nullptr) {
      return false;
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
