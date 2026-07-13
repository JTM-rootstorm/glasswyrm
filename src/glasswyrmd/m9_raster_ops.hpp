#pragma once

#include "glasswyrmd/pixel_storage.hpp"

#include <cstdint>
#include <span>

namespace glasswyrm::server {

struct RasterPoint {
  std::int32_t x{};
  std::int32_t y{};
};

struct RasterSegment {
  RasterPoint first;
  RasterPoint second;
};

struct RasterEllipse {
  std::int32_t x{};
  std::int32_t y{};
  std::uint32_t width{};
  std::uint32_t height{};
};

void draw_line(PixelStorage& destination, RasterPoint first,
               RasterPoint second, std::uint32_t foreground,
               std::uint32_t plane_mask = 0x00ffffffU) noexcept;
void draw_segments(PixelStorage& destination,
                   std::span<const RasterSegment> segments,
                   std::uint32_t foreground,
                   std::uint32_t plane_mask = 0x00ffffffU) noexcept;
void fill_convex_polygon(PixelStorage& destination,
                         std::span<const RasterPoint> points,
                         std::uint32_t foreground,
                         std::uint32_t plane_mask = 0x00ffffffU) noexcept;
void fill_ellipse(PixelStorage& destination, RasterEllipse ellipse,
                  std::uint32_t foreground,
                  std::uint32_t plane_mask = 0x00ffffffU) noexcept;

}  // namespace glasswyrm::server
