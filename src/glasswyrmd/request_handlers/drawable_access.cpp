#include "glasswyrmd/request_handlers/drawable_access.hpp"

#include "core/geometry/region.hpp"
#include "glasswyrmd/extension_event_helpers.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace glasswyrm::server::request_handlers {
std::vector<geometry::Rectangle> rectangle_difference(
    const geometry::Rectangle rectangle, const geometry::Rectangle cutter) {
  const auto overlap = geometry::intersect(rectangle, cutter);
  if (!overlap) return {rectangle};
  std::vector<geometry::Rectangle> result;
  const auto right = rectangle.x + static_cast<std::int64_t>(rectangle.width);
  const auto bottom = rectangle.y + static_cast<std::int64_t>(rectangle.height);
  const auto cut_right = overlap->x + static_cast<std::int64_t>(overlap->width);
  const auto cut_bottom = overlap->y + static_cast<std::int64_t>(overlap->height);
  if (overlap->y > rectangle.y) result.push_back({rectangle.x, rectangle.y, rectangle.width, static_cast<std::uint32_t>(overlap->y-rectangle.y)});
  if (overlap->x > rectangle.x) result.push_back({rectangle.x, overlap->y, static_cast<std::uint32_t>(overlap->x-rectangle.x), overlap->height});
  if (cut_right < right) result.push_back({static_cast<std::int32_t>(cut_right), overlap->y, static_cast<std::uint32_t>(right-cut_right), overlap->height});
  if (cut_bottom < bottom) result.push_back({rectangle.x, static_cast<std::int32_t>(cut_bottom), rectangle.width, static_cast<std::uint32_t>(bottom-cut_bottom)});
  std::sort(result.begin(), result.end(), [](const auto& left, const auto& right_value) {
    return std::tie(left.y,left.x,left.height,left.width) <
           std::tie(right_value.y,right_value.x,right_value.height,right_value.width);
  });
  return result;
}

bool supported_window_drawable(const ResourceTable& resources,
                               const std::uint32_t xid) {
  const auto* window = resources.find_window(xid);
  return window && xid != resources.screen().root_window &&
         window->window_class == WindowClass::InputOutput && window->depth == 24;
}

std::optional<DrawableDamage> translate_window_damage(
    const ResourceTable& resources, std::uint32_t xid,
    geometry::Rectangle rectangle) {
  for (;;) {
    const auto* window = resources.find_window(xid);
    if (!window || window->window_class != WindowClass::InputOutput)
      return std::nullopt;
    const auto clipped = geometry::intersect(
        rectangle, {0, 0, window->width, window->height});
    if (!clipped) return std::nullopt;
    rectangle = *clipped;
    if (window->parent == resources.screen().root_window)
      return DrawableDamage{xid, rectangle};
    rectangle.x += static_cast<std::int32_t>(window->x) + window->border_width;
    rectangle.y += static_cast<std::int32_t>(window->y) + window->border_width;
    xid = window->parent;
  }
}

void add_window_damage(DispatchResult& result, const ResourceTable& resources,
                       const std::uint32_t drawable,
                       const geometry::Rectangle rectangle) {
  if (const auto translated = translate_window_damage(resources, drawable, rectangle))
    result.drawable_damage.push_back(*translated);
}

void add_drawable_damage(DispatchResult& result, ServerState& state,
                         const std::uint32_t drawable,
                         const geometry::Rectangle rectangle,
                         const std::uint32_t timestamp) {
  append_damage_notifications(
      result, state.resources().damage_drawable(drawable, rectangle),
      timestamp);
  add_window_damage(result, state.resources(), drawable, rectangle);
}
bool known_drawable(const ResourceTable& resources, const std::uint32_t xid) {
  return resources.find_window(xid) || resources.find_pixmap(xid);
}

ClipByChildrenGuard::ClipByChildrenGuard(const ResourceTable& resources, const std::uint32_t drawable,
                    const GraphicsContextResource& gc, PixelStorage& storage)
    : storage_(storage) {
  if (gc.subwindow_mode != 0) return;
  const auto* window = resources.find_window(drawable);
  if (!window) return;
  for (const auto child_id : window->children) {
    const auto* child = resources.find_window(child_id);
    if (!child || child->window_class != WindowClass::InputOutput ||
        child->map_state != MapState::Viewable)
      continue;
    const auto clipped = geometry::intersect(
        {child->x, child->y,
         static_cast<std::uint32_t>(child->width) + child->border_width * 2U,
         static_cast<std::uint32_t>(child->height) + child->border_width * 2U},
        {0, 0, storage.width(), storage.height()});
    if (!clipped) continue;
    Saved saved{*clipped, {}};
    saved.pixels.reserve(static_cast<std::size_t>(clipped->width) * clipped->height);
    for (std::uint32_t y = 0; y < clipped->height; ++y)
      for (std::uint32_t x = 0; x < clipped->width; ++x)
        saved.pixels.push_back(storage.at(static_cast<std::uint32_t>(clipped->x) + x,
                                          static_cast<std::uint32_t>(clipped->y) + y));
    saved_.push_back(std::move(saved));
  }
}

void ClipByChildrenGuard::restore() {
  if (restored_) return;
  for (const auto& saved : saved_) {
    std::size_t index = 0;
    for (std::uint32_t y = 0; y < saved.rectangle.height; ++y)
      for (std::uint32_t x = 0; x < saved.rectangle.width; ++x)
        storage_.at(static_cast<std::uint32_t>(saved.rectangle.x) + x,
                    static_cast<std::uint32_t>(saved.rectangle.y) + y) =
            saved.pixels[index++];
  }
  restored_ = true;
}

std::vector<geometry::Rectangle> ClipByChildrenGuard::visible(
  const geometry::Rectangle rectangle) const {
  std::vector<geometry::Rectangle> visible{rectangle};
  for (const auto& saved : saved_) {
    std::vector<geometry::Rectangle> next;
    for (const auto candidate : visible) {
      auto pieces = rectangle_difference(candidate, saved.rectangle);
      next.insert(next.end(), pieces.begin(), pieces.end());
    }
    visible = std::move(next);
  }
  return visible;
}


PixelStorage* mutable_storage(ResourceTable& resources, const std::uint32_t xid) {
  if (auto* pixmap = resources.find_pixmap(xid)) return pixmap->pixels();
  if (!supported_window_drawable(resources, xid)) return nullptr;
  auto* window = resources.find_window(xid);
  if (!window->storage) {
    auto storage = PixelStorage::create(window->width, window->height);
    if (!storage) return nullptr;
    window->storage = std::make_shared<PixelStorage>(std::move(*storage));
    if (window->attributes.background_source == BackgroundSource::Pixel)
      window->storage->fill({0, 0, window->width, window->height},
                            window->attributes.background_pixel);
  }
  return window->storage.get();
}

}  // namespace glasswyrm::server::request_handlers
