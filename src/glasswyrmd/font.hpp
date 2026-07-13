#pragma once

#include "core/geometry/rectangle.hpp"
#include "glasswyrmd/pixel_storage.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace glasswyrm::server {

inline constexpr std::uint32_t kDefaultFontXid = 0xfffffff0U;
inline constexpr std::uint8_t kFixedFontFirstCharacter = 32;
inline constexpr std::uint8_t kFixedFontLastCharacter = 126;
inline constexpr std::uint8_t kFixedFontDefaultCharacter = '?';
inline constexpr std::int16_t kFixedFontAdvance = 6;
inline constexpr std::int16_t kFixedFontAscent = 10;
inline constexpr std::int16_t kFixedFontDescent = 3;

struct FontResource {
  std::uint32_t canonical{kDefaultFontXid};
};

[[nodiscard]] bool matches_fixed_font(std::string_view name) noexcept;
[[nodiscard]] std::array<std::uint8_t, 7> fixed_glyph(std::uint8_t character) noexcept;

struct TextRasterResult {
  geometry::Rectangle damage{};
};

[[nodiscard]] TextRasterResult raster_text8(
    PixelStorage& destination, std::int32_t baseline_x,
    std::int32_t baseline_y, std::span<const std::uint8_t> text,
    std::uint32_t foreground, std::uint32_t background,
    std::uint32_t plane_mask, bool image_text) noexcept;

}  // namespace glasswyrm::server
