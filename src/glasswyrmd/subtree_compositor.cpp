#include "glasswyrmd/subtree_compositor.hpp"

#include "core/geometry/rectangle.hpp"

#include <algorithm>
#include <vector>

namespace glasswyrm::server {
namespace {
struct PendingWindow {
  std::uint32_t xid{};
  std::int32_t origin_x{};
  std::int32_t origin_y{};
  geometry::Rectangle clip{};
};

std::uint32_t background(const WindowResource& window) noexcept {
  return window.attributes.background_source == BackgroundSource::Pixel
             ? window.attributes.background_pixel
             : PixelStorage::kOpaqueBlack;
}

void paint_window(PixelStorage& result, const WindowResource& window,
                  const std::int32_t origin_x, const std::int32_t origin_y,
                  const geometry::Rectangle clip) noexcept {
  const auto visible = geometry::intersect(
      clip, {origin_x, origin_y, window.width, window.height});
  if (!visible) return;
  for (std::uint32_t row = 0; row < visible->height; ++row) {
    const auto destination_y = visible->y + static_cast<std::int32_t>(row);
    const auto source_y = destination_y - origin_y;
    for (std::uint32_t column = 0; column < visible->width; ++column) {
      const auto destination_x = visible->x + static_cast<std::int32_t>(column);
      const auto source_x = destination_x - origin_x;
      const auto pixel = window.storage
                             ? window.storage->at(
                                   static_cast<std::uint32_t>(source_x),
                                   static_cast<std::uint32_t>(source_y))
                             : background(window);
      result.at(static_cast<std::uint32_t>(destination_x),
                static_cast<std::uint32_t>(destination_y)) =
          0xff000000U | (pixel & 0x00ffffffU);
    }
  }
}
}  // namespace

std::optional<PixelStorage> compose_top_level_subtree(
    const ResourceTable& resources, const std::uint32_t top_level_xid) {
  const auto* top = resources.find_window(top_level_xid);
  if (!top || top->parent != resources.screen().root_window ||
      top->window_class != WindowClass::InputOutput)
    return std::nullopt;
  auto result = PixelStorage::create(top->width, top->height);
  if (!result) return std::nullopt;
  const geometry::Rectangle bounds{0, 0, top->width, top->height};
  paint_window(*result, *top, 0, 0, bounds);

  std::vector<PendingWindow> stack;
  stack.reserve(top->children.size());
  const auto enqueue_children = [&](const WindowResource& parent,
                                    const std::int32_t parent_x,
                                    const std::int32_t parent_y,
                                    const geometry::Rectangle parent_clip,
                                    auto& pending) {
    for (auto iterator = parent.children.rbegin();
         iterator != parent.children.rend(); ++iterator) {
      const auto* child = resources.find_window(*iterator);
      if (!child || child->map_state != MapState::Viewable ||
          child->window_class != WindowClass::InputOutput)
        continue;
      const auto x = parent_x + child->x + child->border_width;
      const auto y = parent_y + child->y + child->border_width;
      const auto clip = geometry::intersect(
          parent_clip, {x, y, child->width, child->height});
      if (clip) pending.push_back({*iterator, x, y, *clip});
    }
  };
  enqueue_children(*top, 0, 0, bounds, stack);
  while (!stack.empty()) {
    const auto pending = stack.back();
    stack.pop_back();
    const auto* window = resources.find_window(pending.xid);
    if (!window) return std::nullopt;
    paint_window(*result, *window, pending.origin_x, pending.origin_y,
                 pending.clip);
    enqueue_children(*window, pending.origin_x, pending.origin_y, pending.clip,
                     stack);
  }
  return result;
}

}  // namespace glasswyrm::server
