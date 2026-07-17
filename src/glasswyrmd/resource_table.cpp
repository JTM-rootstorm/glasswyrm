#include "glasswyrmd/resource_table.hpp"

#include "glasswyrmd/resource_id.hpp"

#include <algorithm>
#include <string>

namespace glasswyrm::server {

ResourceTable::ResourceTable(const ScreenModel screen, ResourceLimits limits)
    : screen_(screen), limits_(limits) {
  std::string cursor_error;
  root_default_cursor_ = input::make_glyph_cursor(
      {input::CursorFontIdentity::Cursor,
       input::CursorFontIdentity::Cursor,
       input::kCursorGlyphLeftPointer,
       static_cast<std::uint16_t>(input::kCursorGlyphLeftPointer + 1U),
       {},
       {0xffff, 0xffff, 0xffff}},
      cursor_error);
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
  resources_.emplace(
      screen.default_colormap,
      ResourceRecord{ResourceType::Colormap, std::nullopt,
                     ColormapResource{screen.root_visual}});
  resources_.emplace(kDefaultFontXid,
                     ResourceRecord{ResourceType::Font, std::nullopt,
                                    FontResource{}});
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

const PixmapResource* ResourceTable::find_pixmap(
    const std::uint32_t xid) const noexcept {
  const auto* resource = find(xid);
  return resource ? std::get_if<PixmapResource>(&resource->payload) : nullptr;
}

PixmapResource* ResourceTable::find_pixmap(const std::uint32_t xid) noexcept {
  auto* resource = find(xid);
  return resource ? std::get_if<PixmapResource>(&resource->payload) : nullptr;
}

const GraphicsContextResource* ResourceTable::find_gc(
    const std::uint32_t xid) const noexcept {
  const auto* resource = find(xid);
  return resource
             ? std::get_if<GraphicsContextResource>(&resource->payload)
             : nullptr;
}

GraphicsContextResource* ResourceTable::find_gc(
    const std::uint32_t xid) noexcept {
  auto* resource = find(xid);
  return resource
             ? std::get_if<GraphicsContextResource>(&resource->payload)
             : nullptr;
}

const FontResource* ResourceTable::find_font(
    const std::uint32_t xid) const noexcept {
  const auto* resource = find(xid);
  return resource ? std::get_if<FontResource>(&resource->payload) : nullptr;
}

const CursorResource* ResourceTable::find_cursor(
    const std::uint32_t xid) const noexcept {
  const auto* resource = find(xid);
  return resource ? std::get_if<CursorResource>(&resource->payload) : nullptr;
}

const ColormapResource* ResourceTable::find_colormap(
    const std::uint32_t xid) const noexcept {
  const auto* resource = find(xid);
  return resource ? std::get_if<ColormapResource>(&resource->payload) : nullptr;
}

bool ResourceTable::valid_colormap(const std::uint32_t xid) const noexcept {
  return xid == screen_.default_colormap || find_colormap(xid) != nullptr;
}

const ShmSegmentResource* ResourceTable::find_shm_segment(
    const std::uint32_t xid) const noexcept {
  const auto* resource = find(xid);
  return resource ? std::get_if<ShmSegmentResource>(&resource->payload)
                  : nullptr;
}

ShmSegmentResource* ResourceTable::find_shm_segment(
    const std::uint32_t xid) noexcept {
  auto* resource = find(xid);
  return resource ? std::get_if<ShmSegmentResource>(&resource->payload)
                  : nullptr;
}

const XFixesRegionResource* ResourceTable::find_xfixes_region(
    const std::uint32_t xid) const noexcept {
  const auto* resource = find(xid);
  return resource ? std::get_if<XFixesRegionResource>(&resource->payload)
                  : nullptr;
}

XFixesRegionResource* ResourceTable::find_xfixes_region(
    const std::uint32_t xid) noexcept {
  auto* resource = find(xid);
  return resource ? std::get_if<XFixesRegionResource>(&resource->payload)
                  : nullptr;
}

const DamageResource* ResourceTable::find_damage(
    const std::uint32_t xid) const noexcept {
  const auto* resource = find(xid);
  return resource ? std::get_if<DamageResource>(&resource->payload) : nullptr;
}

DamageResource* ResourceTable::find_damage(const std::uint32_t xid) noexcept {
  auto* resource = find(xid);
  return resource ? std::get_if<DamageResource>(&resource->payload) : nullptr;
}

std::shared_ptr<const input::CursorImage> ResourceTable::effective_cursor(
    const std::uint32_t pointer_target) const noexcept {
  auto current = pointer_target;
  for (std::size_t depth = 0; depth <= resources_.size(); ++depth) {
    const auto* window = find_window(current);
    if (!window) return nullptr;
    if (!window->attributes.cursor_inherit)
      return window->attributes.cursor_image;
    if (current == screen_.root_window) return root_default_cursor_;
    current = window->parent;
  }
  return nullptr;
}

bool ResourceTable::valid_new_resource_id(
    const std::uint32_t xid, const std::uint32_t resource_base,
    const std::uint32_t resource_mask) const {
  return resource_id_matches_client(xid, resource_base, resource_mask) &&
         !is_server_owned_id(xid, screen_) && !resources_.contains(xid);
}

std::size_t ResourceTable::resource_count(
    const ResourceType type) const noexcept {
  return static_cast<std::size_t>(std::count_if(
      resources_.begin(), resources_.end(),
      [type](const auto& entry) { return entry.second.type == type; }));
}

std::size_t ResourceTable::resource_count_by_owner(
    const ClientId owner) const noexcept {
  const auto iterator = resources_by_owner_.find(owner);
  return iterator == resources_by_owner_.end() ? 0 : iterator->second.size();
}

}  // namespace glasswyrm::server
