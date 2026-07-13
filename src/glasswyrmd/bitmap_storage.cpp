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

}  // namespace glasswyrm::server
