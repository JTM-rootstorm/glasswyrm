#include "glasswyrmd/resource_table.hpp"

#include <algorithm>
#include <new>

namespace glasswyrm::server {

CreatePixmapStatus ResourceTable::create_pixmap(
    const ClientId owner, const std::uint32_t resource_base,
    const std::uint32_t resource_mask, const std::uint32_t xid,
    const std::uint32_t drawable, const std::uint8_t depth,
    const std::uint16_t width, const std::uint16_t height) {
  if (!valid_new_resource_id(xid, resource_base, resource_mask))
    return CreatePixmapStatus::BadIdChoice;
  const auto* anchor_window = find_window(drawable);
  const auto valid_anchor =
      drawable == screen_.root_window || anchor_window || find_pixmap(drawable);
  if (!valid_anchor) return CreatePixmapStatus::BadDrawable;
  if (anchor_window && drawable != screen_.root_window &&
      (anchor_window->parent != screen_.root_window ||
       anchor_window->window_class != WindowClass::InputOutput ||
       anchor_window->depth != 24))
    return CreatePixmapStatus::BadMatch;
  if ((depth != 1 && depth != 24) || width == 0 || height == 0)
    return CreatePixmapStatus::BadValue;
  if (resource_count(ResourceType::Pixmap) >= limits_.maximum_pixmaps)
    return CreatePixmapStatus::BadAlloc;
  std::variant<std::shared_ptr<BitmapStorage>, std::shared_ptr<PixelStorage>>
      storage;
  if (depth == 1) {
    auto bitmap = BitmapStorage::create(width, height);
    if (!bitmap) return CreatePixmapStatus::BadAlloc;
    storage = std::make_shared<BitmapStorage>(std::move(*bitmap));
  } else {
    auto pixels = PixelStorage::create(width, height);
    if (!pixels) return CreatePixmapStatus::BadAlloc;
    storage = std::make_shared<PixelStorage>(std::move(*pixels));
  }
  const auto bytes = std::visit(
      [](const auto& value) { return value->byte_size(); }, storage);
  if (bytes > limits_.maximum_canonical_drawable_bytes -
                  canonical_drawable_bytes_)
    return CreatePixmapStatus::BadAlloc;
  try {
    PixmapResource pixmap{screen_.root_window, depth, width, height,
                          std::move(storage)};
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
  (void)remove_damage_for_drawable(xid);
  const auto owner = find(xid)->owner;
  const auto bytes = pixmap->byte_size();
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
    if (window->window_class != WindowClass::InputOutput ||
        window->depth != screen_.root_depth)
      return CreateGcStatus::BadMatch;
    depth = window->depth;
  }
  else if (const auto* pixmap = find_pixmap(drawable)) depth = pixmap->depth;
  else return CreateGcStatus::BadDrawable;
  if (depth != 1 && depth != 24) return CreateGcStatus::BadMatch;
  if (depth == 1) {
    gc.foreground &= 1U;
    gc.background &= 1U;
    gc.plane_mask &= 1U;
  }
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

}  // namespace glasswyrm::server
