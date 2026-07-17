#include "glasswyrmd/resource_table.hpp"

#include <algorithm>

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

bool ResourceTable::invariants_hold() const noexcept {
  const auto* root = find_window(screen_.root_window);
  if (root == nullptr || root->parent != 0 || !find(screen_.root_window) ||
      find(screen_.root_window)->owner.has_value() || !root_default_cursor_) {
    return false;
  }
  std::size_t calculated_property_bytes = 0;
  std::size_t calculated_drawable_bytes = 0;
  std::size_t calculated_cursor_bytes = 0;
  for (const auto& [xid, resource] : resources_) {
    const auto* window = std::get_if<WindowResource>(&resource.payload);
    if (window == nullptr) {
      if ((!resource.owner && resource.type != ResourceType::Font &&
           resource.type != ResourceType::Colormap) ||
          resource.type == ResourceType::Window) return false;
      if (!resource.owner) {
        const bool default_font =
            xid == kDefaultFontXid &&
            std::holds_alternative<FontResource>(resource.payload);
        const bool default_colormap =
            xid == screen_.default_colormap &&
            std::holds_alternative<ColormapResource>(resource.payload);
        if (!default_font && !default_colormap) return false;
        continue;
      }
      const auto owner_iterator = resources_by_owner_.find(*resource.owner);
      if (owner_iterator == resources_by_owner_.end() ||
          std::count(owner_iterator->second.begin(), owner_iterator->second.end(), xid) != 1)
        return false;
      if (const auto* pixmap = std::get_if<PixmapResource>(&resource.payload))
        calculated_drawable_bytes += pixmap->byte_size();
      if (const auto* cursor = std::get_if<CursorResource>(&resource.payload)) {
        if (!cursor->image) return false;
        calculated_cursor_bytes += cursor->image->byte_size();
      }
      if (const auto* segment =
              std::get_if<ShmSegmentResource>(&resource.payload)) {
        if (!segment->mapping || segment->size == 0 || segment->shmid < 0)
          return false;
      }
      continue;
    }
    if (window->attributes.cursor_inherit) {
      if (window->attributes.cursor != 0 || window->attributes.cursor_image)
        return false;
    } else {
      if (!window->attributes.cursor_image) return false;
      if (window->attributes.cursor != 0) {
        const auto* cursor = find_cursor(window->attributes.cursor);
        if (!cursor || cursor->image != window->attributes.cursor_image)
          return false;
      }
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
  if (calculated_cursor_bytes != total_cursor_bytes_) return false;
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
