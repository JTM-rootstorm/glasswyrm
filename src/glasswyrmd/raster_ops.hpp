#pragma once

#include "core/geometry/rectangle.hpp"
#include "glasswyrmd/pixel_storage.hpp"

#include <cstdint>
#include <span>

namespace glasswyrm::server {

struct RasterResult {
  bool success{true};
  glasswyrm::geometry::Rectangle damage{};
};

[[nodiscard]] RasterResult put_zpixmap(
    PixelStorage& destination, std::int32_t x, std::int32_t y,
    std::uint32_t width, std::uint32_t height,
    std::span<const std::uint8_t> little_endian_pixels,
    std::uint32_t plane_mask = 0x00ffffffU) noexcept;
[[nodiscard]] RasterResult copy_area(
    const PixelStorage& source, PixelStorage& destination,
    std::int32_t source_x, std::int32_t source_y, std::uint32_t width,
    std::uint32_t height, std::int32_t destination_x,
    std::int32_t destination_y, std::uint32_t plane_mask = 0x00ffffffU);

}  // namespace glasswyrm::server
