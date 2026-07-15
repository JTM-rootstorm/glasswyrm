#include "glasswyrmd/font.hpp"

#include <algorithm>
#include <cctype>
#include <limits>

namespace glasswyrm::server {
namespace {

std::array<std::uint8_t, 7> rows(const std::uint64_t bits) noexcept {
  std::array<std::uint8_t, 7> result{};
  for (std::size_t row = 0; row < result.size(); ++row)
    result[row] = static_cast<std::uint8_t>((bits >> ((6U - row) * 5U)) & 0x1fU);
  return result;
}

std::uint32_t apply_pixel(const std::uint32_t source,
                          const std::uint32_t destination,
                          const std::uint32_t plane_mask) noexcept {
  const auto mask = plane_mask & 0x00ffffffU;
  return 0xff000000U | (source & mask) |
         (destination & ~mask & 0x00ffffffU);
}

}  // namespace

bool matches_fixed_font(const std::string_view name) noexcept {
  if (name.size() > 255) return false;
  std::array<char, 256> lower{};
  std::transform(name.begin(), name.end(), lower.begin(), [](const char value) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
  });
  const std::string_view normalized(lower.data(), name.size());
  return normalized == "fixed" || normalized == "6x13" ||
         (normalized.starts_with("-misc-fixed-") && normalized.ends_with('*'));
}

std::optional<FontIdentity> font_identity(const std::string_view name) noexcept {
  if (name.size() > 255) return std::nullopt;
  std::array<char, 256> lower{};
  std::transform(name.begin(), name.end(), lower.begin(), [](const char value) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
  });
  const std::string_view normalized(lower.data(), name.size());
  if (normalized == "cursor") return FontIdentity::Cursor;
  if (normalized == "nil2") return FontIdentity::Nil2;
  return matches_fixed_font(name) ? std::optional{FontIdentity::Fixed}
                                  : std::nullopt;
}

std::array<std::uint8_t, 7> fixed_glyph(std::uint8_t character) noexcept {
  if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
  switch (character) {
    case ' ': return {};
    case '0': return rows(0b01110100011001110101110011000101110ULL);
    case '1': return rows(0b00100011000010000100001000010001110ULL);
    case '2': return rows(0b01110100010000100010001000100011111ULL);
    case '3': return rows(0b11110000010000101110000010000111110ULL);
    case '4': return rows(0b00010001100101010010111110001000010ULL);
    case '5': return rows(0b11111100001111000001000011000101110ULL);
    case '6': return rows(0b00110010001000011110100011000101110ULL);
    case '7': return rows(0b11111000010001000100010000100001000ULL);
    case '8': return rows(0b01110100011000101110100011000101110ULL);
    case '9': return rows(0b01110100011000101111000010001001100ULL);
    case 'A': return rows(0b01110100011000111111100011000110001ULL);
    case 'B': return rows(0b11110100011000111110100011000111110ULL);
    case 'C': return rows(0b01111100001000010000100001000001111ULL);
    case 'D': return rows(0b11110100011000110001100011000111110ULL);
    case 'E': return rows(0b11111100001000011110100001000011111ULL);
    case 'F': return rows(0b11111100001000011110100001000010000ULL);
    case 'G': return rows(0b01111100001000010111100011000101111ULL);
    case 'H': return rows(0b10001100011000111111100011000110001ULL);
    case 'I': return rows(0b01110001000010000100001000010001110ULL);
    case 'J': return rows(0b00001000010000100001100011000101110ULL);
    case 'K': return rows(0b10001100101010011000101001001010001ULL);
    case 'L': return rows(0b10000100001000010000100001000011111ULL);
    case 'M': return rows(0b10001110111010110101100011000110001ULL);
    case 'N': return rows(0b10001110011010110011100011000110001ULL);
    case 'O': return rows(0b01110100011000110001100011000101110ULL);
    case 'P': return rows(0b11110100011000111110100001000010000ULL);
    case 'Q': return rows(0b01110100011000110001101011001001101ULL);
    case 'R': return rows(0b11110100011000111110101001001010001ULL);
    case 'S': return rows(0b01111100001000001110000010000111110ULL);
    case 'T': return rows(0b11111001000010000100001000010000100ULL);
    case 'U': return rows(0b10001100011000110001100011000101110ULL);
    case 'V': return rows(0b10001100011000110001100010101000100ULL);
    case 'W': return rows(0b10001100011000110101101011101110001ULL);
    case 'X': return rows(0b10001100010101000100010101000110001ULL);
    case 'Y': return rows(0b10001100010101000100001000010000100ULL);
    case 'Z': return rows(0b11111000010001000100010001000011111ULL);
    case ':': return rows(0b00000001000010000000001000010000000ULL);
    case '.': return rows(0b00000000000000000000000000010000100ULL);
    case ',': return rows(0b00000000000000000000001000010001000ULL);
    case '-': return rows(0b00000000000000011111000000000000000ULL);
    case '_': return rows(0b00000000000000000000000000000011111ULL);
    case '/': return rows(0b00001000100001000100010001000010000ULL);
    case '\\': return rows(0b10000010000100000100000100001000001ULL);
    case '!': return rows(0b00100001000010000100001000000000100ULL);
    case '?': return rows(0b01110100010000100010001000000000100ULL);
    case '+': return rows(0b00000001000010011111001000010000000ULL);
    case '=': return rows(0b00000000001111100000111110000000000ULL);
    case '(': return rows(0b00010001000100001000010000010000010ULL);
    case ')': return rows(0b01000001000001000010000100010001000ULL);
    case '[': return rows(0b01110010000100001000010000100001110ULL);
    case ']': return rows(0b01110000100001000010000100001001110ULL);
    default:
      if (character >= kFixedFontFirstCharacter &&
          character <= kFixedFontLastCharacter) {
        // Every printable byte has a stable repository-owned mark. Less common
        // punctuation is intentionally abstract but distinct from replacement.
        const auto seed = static_cast<std::uint8_t>(character * 37U + 11U);
        return {0x1fU, 0x11U, static_cast<std::uint8_t>(0x10U | (seed & 0xfU)),
                static_cast<std::uint8_t>(0x10U | ((seed >> 2U) & 0xfU)),
                static_cast<std::uint8_t>(0x10U | ((seed >> 4U) & 0xfU)),
                0x11U, 0x1fU};
      }
      return rows(0b11111100011010110101101011000111111ULL);
  }
}

TextRasterResult raster_text8(
    PixelStorage& destination, const std::int32_t baseline_x,
    const std::int32_t baseline_y, const std::span<const std::uint8_t> text,
    const std::uint32_t foreground, const std::uint32_t background,
    const std::uint32_t plane_mask, const bool image_text) noexcept {
  if (text.empty()) return {};
  const auto width64 = static_cast<std::uint64_t>(text.size()) *
                       static_cast<std::uint32_t>(kFixedFontAdvance);
  if (width64 > std::numeric_limits<std::uint32_t>::max()) return {};
  const geometry::Rectangle cell_bounds{
      baseline_x, baseline_y - kFixedFontAscent,
      static_cast<std::uint32_t>(width64),
      static_cast<std::uint32_t>(kFixedFontAscent + kFixedFontDescent)};
  const auto clipped = geometry::intersect(
      cell_bounds, {0, 0, destination.width(), destination.height()});
  if (!clipped) return {};
  if (image_text) destination.fill(*clipped, background, plane_mask);
  for (std::size_t index = 0; index < text.size(); ++index) {
    const auto glyph = fixed_glyph(text[index]);
    const auto glyph_x = baseline_x + static_cast<std::int32_t>(index) * 6;
    const auto glyph_y = baseline_y - 8;
    for (std::int32_t row = 0; row < 7; ++row)
      for (std::int32_t column = 0; column < 5; ++column) {
        if ((glyph[static_cast<std::size_t>(row)] & (1U << (4 - column))) == 0)
          continue;
        const auto x = glyph_x + column;
        const auto y = glyph_y + row;
        if (x < 0 || y < 0 || x >= static_cast<std::int32_t>(destination.width()) ||
            y >= static_cast<std::int32_t>(destination.height())) continue;
        auto& pixel = destination.at(static_cast<std::uint32_t>(x),
                                     static_cast<std::uint32_t>(y));
        pixel = apply_pixel(foreground, pixel, plane_mask);
      }
  }
  return {*clipped};
}

}  // namespace glasswyrm::server
