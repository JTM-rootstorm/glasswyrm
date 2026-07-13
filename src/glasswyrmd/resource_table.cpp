#include "glasswyrmd/resource_table.hpp"

#include "glasswyrmd/resource_id.hpp"

#include <algorithm>

namespace glasswyrm::server {

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
