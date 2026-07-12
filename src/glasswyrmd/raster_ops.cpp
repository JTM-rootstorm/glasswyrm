#include "glasswyrmd/raster_ops.hpp"

#include <vector>

namespace glasswyrm::server {
namespace {
std::uint32_t decode_pixel(const std::span<const std::uint8_t> bytes,
                           const std::size_t offset) noexcept {
  return static_cast<std::uint32_t>(bytes[offset]) |
         static_cast<std::uint32_t>(bytes[offset + 1]) << 8U |
         static_cast<std::uint32_t>(bytes[offset + 2]) << 16U |
         static_cast<std::uint32_t>(bytes[offset + 3]) << 24U;
}
std::uint32_t apply(const std::uint32_t source, const std::uint32_t destination,
                    const std::uint32_t plane_mask) noexcept {
  const auto mask = plane_mask & 0x00ffffffU;
  return 0xff000000U | (source & mask) | (destination & ~mask & 0x00ffffffU);
}
}  // namespace

RasterResult put_zpixmap(PixelStorage& destination, const std::int32_t x,
                         const std::int32_t y, const std::uint32_t width,
                         const std::uint32_t height,
                         const std::span<const std::uint8_t> pixels,
                         const std::uint32_t plane_mask) noexcept {
  const auto required = static_cast<std::uint64_t>(width) * height * 4U;
  if (required != pixels.size()) return {false, {}};
  const auto clipped = geometry::intersect(
      {x, y, width, height}, {0, 0, destination.width(), destination.height()});
  if (!clipped) return {};
  for (std::uint32_t row = 0; row < clipped->height; ++row)
    for (std::uint32_t column = 0; column < clipped->width; ++column) {
      const auto source_x = static_cast<std::uint32_t>(clipped->x - x) + column;
      const auto source_y = static_cast<std::uint32_t>(clipped->y - y) + row;
      const auto offset = (static_cast<std::size_t>(source_y) * width + source_x) * 4U;
      auto& target = destination.at(static_cast<std::uint32_t>(clipped->x) + column,
                                    static_cast<std::uint32_t>(clipped->y) + row);
      target = apply(decode_pixel(pixels, offset), target, plane_mask);
    }
  return {true, *clipped};
}

RasterResult copy_area(const PixelStorage& source, PixelStorage& destination,
                       const std::int32_t source_x, const std::int32_t source_y,
                       const std::uint32_t width, const std::uint32_t height,
                       const std::int32_t destination_x,
                       const std::int32_t destination_y,
                       const std::uint32_t plane_mask) {
  auto copied_source = geometry::intersect(
      {source_x, source_y, width, height}, {0, 0, source.width(), source.height()});
  if (!copied_source) return {};
  geometry::Rectangle translated{
      destination_x + (copied_source->x - source_x),
      destination_y + (copied_source->y - source_y),
      copied_source->width, copied_source->height};
  auto copied_destination = geometry::intersect(
      translated, {0, 0, destination.width(), destination.height()});
  if (!copied_destination) return {};
  const auto final_source_x = copied_source->x + (copied_destination->x - translated.x);
  const auto final_source_y = copied_source->y + (copied_destination->y - translated.y);
  std::vector<std::uint32_t> scratch;
  scratch.reserve(static_cast<std::size_t>(copied_destination->width) *
                  copied_destination->height);
  for (std::uint32_t row = 0; row < copied_destination->height; ++row)
    for (std::uint32_t column = 0; column < copied_destination->width; ++column)
      scratch.push_back(source.at(static_cast<std::uint32_t>(final_source_x) + column,
                                  static_cast<std::uint32_t>(final_source_y) + row));
  std::size_t index = 0;
  for (std::uint32_t row = 0; row < copied_destination->height; ++row)
    for (std::uint32_t column = 0; column < copied_destination->width; ++column) {
      auto& target = destination.at(
          static_cast<std::uint32_t>(copied_destination->x) + column,
          static_cast<std::uint32_t>(copied_destination->y) + row);
      target = apply(scratch[index++], target, plane_mask);
    }
  return {true, *copied_destination};
}
}  // namespace glasswyrm::server
