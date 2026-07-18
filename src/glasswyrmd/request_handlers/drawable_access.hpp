#pragma once

#include "glasswyrmd/request_handlers/common.hpp"
#include "glasswyrmd/graphics_context.hpp"
#include "glasswyrmd/pixel_storage.hpp"

#include <cstdint>
#include <vector>

namespace glasswyrm::server::request_handlers {

[[nodiscard]] bool supported_window_drawable(const ResourceTable& resources,
                                             std::uint32_t xid);
[[nodiscard]] bool known_drawable(const ResourceTable& resources,
                                  std::uint32_t xid);
[[nodiscard]] PixelStorage* mutable_storage(ResourceTable& resources,
                                            std::uint32_t xid);
[[nodiscard]] std::vector<geometry::Rectangle> rectangle_difference(
    geometry::Rectangle rectangle, geometry::Rectangle cutter);
void add_window_damage(DispatchResult& result, const ResourceTable& resources,
                       std::uint32_t drawable,
                       geometry::Rectangle rectangle);
void add_drawable_damage(DispatchResult& result, ServerState& state,
                         std::uint32_t drawable,
                         geometry::Rectangle rectangle,
                         std::uint32_t timestamp);

class ClipByChildrenGuard {
 public:
  ClipByChildrenGuard(const ResourceTable& resources, std::uint32_t drawable,
                      const GraphicsContextResource& gc, PixelStorage& storage);
  void restore();
  [[nodiscard]] std::vector<geometry::Rectangle> visible(
      geometry::Rectangle rectangle) const;

 private:
  struct Saved {
    geometry::Rectangle rectangle;
    std::vector<std::uint32_t> pixels;
  };

  PixelStorage& storage_;
  std::vector<Saved> saved_;
  bool restored_{false};
};

}  // namespace glasswyrm::server::request_handlers
