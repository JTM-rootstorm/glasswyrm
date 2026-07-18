#include "glasswyrmd/alpha_storage.hpp"

#include <algorithm>
#include <new>

namespace glasswyrm::server {

std::optional<AlphaStorage> AlphaStorage::create(
    const std::uint32_t width, const std::uint32_t height,
    const std::uint8_t initial_alpha) {
  if (width == 0 || height == 0 || width > kMaximumDimension ||
      height > kMaximumDimension)
    return std::nullopt;
  const auto count = static_cast<std::uint64_t>(width) * height;
  if (count > kMaximumBytes) return std::nullopt;
  try {
    return AlphaStorage(
        width, height,
        std::vector<std::uint8_t>(static_cast<std::size_t>(count),
                                  initial_alpha));
  } catch (const std::bad_alloc&) {
    return std::nullopt;
  }
}

std::optional<AlphaStorage> AlphaStorage::resize_preserving_overlap(
    const std::uint32_t width, const std::uint32_t height,
    const std::uint8_t initial_alpha) const {
  auto result = create(width, height, initial_alpha);
  if (!result) return std::nullopt;
  const auto copy_width = std::min(width_, width);
  const auto copy_height = std::min(height_, height);
  for (std::uint32_t y = 0; y < copy_height; ++y)
    std::copy_n(alpha_.begin() + static_cast<std::size_t>(y) * width_,
                copy_width,
                result->alpha_.begin() + static_cast<std::size_t>(y) * width);
  return result;
}

void AlphaStorage::fill(const geometry::Rectangle rectangle,
                        const std::uint8_t alpha) noexcept {
  const auto clipped =
      geometry::intersect(rectangle, {0, 0, width_, height_});
  if (!clipped) return;
  for (std::uint32_t y = 0; y < clipped->height; ++y) {
    const auto offset =
        static_cast<std::size_t>(clipped->y + static_cast<std::int32_t>(y)) *
            width_ +
        static_cast<std::uint32_t>(clipped->x);
    std::fill_n(alpha_.begin() + static_cast<std::ptrdiff_t>(offset),
                clipped->width, alpha);
  }
}

}  // namespace glasswyrm::server
