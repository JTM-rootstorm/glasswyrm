#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace gw::render::software {

enum class PixelFormat { Xrgb8888, Argb8888Premultiplied };

struct Pixel {
  std::uint8_t red{};
  std::uint8_t green{};
  std::uint8_t blue{};
  std::uint8_t alpha{};

  friend constexpr bool operator==(const Pixel&, const Pixel&) = default;
};

[[nodiscard]] constexpr Pixel unpack_xrgb8888(const std::uint32_t value) noexcept {
  return Pixel{static_cast<std::uint8_t>(value >> 16),
               static_cast<std::uint8_t>(value >> 8),
               static_cast<std::uint8_t>(value), 255};
}

[[nodiscard]] constexpr Pixel unpack_argb8888(const std::uint32_t value) noexcept {
  return Pixel{static_cast<std::uint8_t>(value >> 16),
               static_cast<std::uint8_t>(value >> 8),
               static_cast<std::uint8_t>(value),
               static_cast<std::uint8_t>(value >> 24)};
}

[[nodiscard]] constexpr std::uint32_t pack_xrgb8888(const Pixel pixel) noexcept {
  return 0xff000000U | (static_cast<std::uint32_t>(pixel.red) << 16) |
         (static_cast<std::uint32_t>(pixel.green) << 8) | pixel.blue;
}

[[nodiscard]] inline std::uint32_t load_u32(const std::byte* address) noexcept {
  std::uint32_t value{};
  std::memcpy(&value, address, sizeof(value));
  return value;
}

inline void store_u32(std::byte* address, const std::uint32_t value) noexcept {
  std::memcpy(address, &value, sizeof(value));
}

[[nodiscard]] constexpr bool is_premultiplied(const Pixel pixel) noexcept {
  return pixel.red <= pixel.alpha && pixel.green <= pixel.alpha &&
         pixel.blue <= pixel.alpha;
}

} // namespace gw::render::software
