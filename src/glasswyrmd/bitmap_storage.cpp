#include "glasswyrmd/bitmap_storage.hpp"

#include <new>

namespace glasswyrm::server {

std::optional<BitmapStorage> BitmapStorage::create(const std::uint32_t width,
                                                   const std::uint32_t height) {
  if (width == 0 || height == 0 || width > kMaximumDimension ||
      height > kMaximumDimension)
    return std::nullopt;
  const auto count = static_cast<std::uint64_t>(width) * height;
  if (count > kMaximumBytes) return std::nullopt;
  try {
    return BitmapStorage(
        width, height,
        std::vector<std::uint8_t>(static_cast<std::size_t>(count), 0));
  } catch (const std::bad_alloc&) {
    return std::nullopt;
  }
}

bool put_xybitmap_lsb32(BitmapStorage& destination,
                        const std::int32_t destination_x,
                        const std::int32_t destination_y,
                        const std::uint32_t width,
                        const std::uint32_t height,
                        const std::span<const std::uint8_t> payload,
                        const std::uint32_t foreground,
                        const std::uint32_t background,
                        const std::uint32_t plane_mask) noexcept {
  const auto stride = (static_cast<std::uint64_t>(width) + 31U) / 32U * 4U;
  const auto required = stride * height;
  if (required != payload.size()) return false;
  if ((plane_mask & 1U) == 0) return true;
  for (std::uint32_t row = 0; row < height; ++row) {
    const auto target_y = static_cast<std::int64_t>(destination_y) + row;
    if (target_y < 0 || target_y >= destination.height()) continue;
    for (std::uint32_t column = 0; column < width; ++column) {
      const auto target_x = static_cast<std::int64_t>(destination_x) + column;
      if (target_x < 0 || target_x >= destination.width()) continue;
      const auto source = payload[static_cast<std::size_t>(row * stride) +
                                  column / 8U] >> (column % 8U) & 1U;
      destination.set(static_cast<std::uint32_t>(target_x),
                      static_cast<std::uint32_t>(target_y),
                      source != 0 ? foreground : background);
    }
  }
  return true;
}

bool put_zpixmap_lsb32(BitmapStorage& destination,
                       const std::int32_t destination_x,
                       const std::int32_t destination_y,
                       const std::uint32_t width,
                       const std::uint32_t height,
                       const std::span<const std::uint8_t> payload,
                       const std::uint32_t plane_mask) noexcept {
  const auto stride = (static_cast<std::uint64_t>(width) + 31U) / 32U * 4U;
  const auto required = stride * height;
  if (required != payload.size()) return false;
  if ((plane_mask & 1U) == 0) return true;
  for (std::uint32_t row = 0; row < height; ++row) {
    const auto target_y = static_cast<std::int64_t>(destination_y) + row;
    if (target_y < 0 || target_y >= destination.height()) continue;
    for (std::uint32_t column = 0; column < width; ++column) {
      const auto target_x = static_cast<std::int64_t>(destination_x) + column;
      if (target_x < 0 || target_x >= destination.width()) continue;
      const auto source = payload[static_cast<std::size_t>(row * stride) +
                                  column / 8U] >> (column % 8U) & 1U;
      destination.set(static_cast<std::uint32_t>(target_x),
                      static_cast<std::uint32_t>(target_y), source);
    }
  }
  return true;
}

}  // namespace glasswyrm::server
