#include "glasswyrmd/pixel_storage.hpp"

#include <algorithm>
#include <limits>
#include <new>

namespace glasswyrm::server {
std::optional<PixelStorage> PixelStorage::create(const std::uint32_t width,
                                                 const std::uint32_t height) {
  if (width == 0 || height == 0 || width > kMaximumDimension ||
      height > kMaximumDimension) return std::nullopt;
  const auto count = static_cast<std::uint64_t>(width) * height;
  if (count > kMaximumBytes / sizeof(std::uint32_t)) return std::nullopt;
  try {
    return PixelStorage(width, height,
                        std::vector<std::uint32_t>(static_cast<std::size_t>(count),
                                                   kOpaqueBlack));
  } catch (const std::bad_alloc&) { return std::nullopt; }
}

std::optional<PixelStorage> PixelStorage::resize_preserving_overlap(
    const std::uint32_t width, const std::uint32_t height,
    const std::uint32_t initial) const {
  auto result = create(width, height);
  if (!result) return std::nullopt;
  std::fill(result->pixels_.begin(), result->pixels_.end(),
            0xff000000U | (initial & 0x00ffffffU));
  const auto copy_width = std::min(width_, width);
  const auto copy_height = std::min(height_, height);
  for (std::uint32_t y = 0; y < copy_height; ++y)
    std::copy_n(pixels_.begin() + static_cast<std::size_t>(y) * width_, copy_width,
                result->pixels_.begin() + static_cast<std::size_t>(y) * width);
  return result;
}

void PixelStorage::fill(const geometry::Rectangle rectangle,
                        const std::uint32_t rgb,
                        const std::uint32_t plane_mask) noexcept {
  const auto clipped = geometry::intersect(
      rectangle, {0, 0, width_, height_});
  if (!clipped) return;
  const auto mask = plane_mask & 0x00ffffffU;
  for (std::uint32_t y = 0; y < clipped->height; ++y)
    for (std::uint32_t x = 0; x < clipped->width; ++x) {
      auto& destination = at(static_cast<std::uint32_t>(clipped->x) + x,
                             static_cast<std::uint32_t>(clipped->y) + y);
      destination = 0xff000000U | (rgb & mask) | (destination & ~mask & 0x00ffffffU);
    }
}
}  // namespace glasswyrm::server
