#pragma once

#include "core/geometry/rectangle.hpp"
#include "glasswyrmd/render_ops.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace glasswyrm::server {

enum class PictureFormatId : std::uint32_t {
  A1 = 0x1FFFF101U,
  A8 = 0x1FFFF102U,
  Xrgb32 = 0x1FFFF103U,
  Argb32 = 0x1FFFF104U,
};

struct PictureFormatDescriptor {
  PictureFormatId id{};
  std::uint8_t depth{};
  std::uint8_t bits_per_pixel{};
  std::uint8_t red_shift{};
  std::uint16_t red_mask{};
  std::uint8_t green_shift{};
  std::uint16_t green_mask{};
  std::uint8_t blue_shift{};
  std::uint16_t blue_mask{};
  std::uint8_t alpha_shift{};
  std::uint16_t alpha_mask{};

  friend bool operator==(const PictureFormatDescriptor&,
                         const PictureFormatDescriptor&) = default;
};

inline constexpr std::array<PictureFormatDescriptor, 4>
    kCanonicalPictureFormats{{
        {PictureFormatId::A1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0x0001},
        {PictureFormatId::A8, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0x00FF},
        {PictureFormatId::Xrgb32, 24, 32, 16, 0x00FF, 8, 0x00FF, 0,
         0x00FF, 0, 0},
        {PictureFormatId::Argb32, 32, 32, 16, 0x00FF, 8, 0x00FF, 0,
         0x00FF, 24, 0x00FF},
    }};

[[nodiscard]] const PictureFormatDescriptor* find_picture_format(
    PictureFormatId id) noexcept;

struct DrawablePictureSource {
  std::uint32_t drawable{};
  friend bool operator==(const DrawablePictureSource&,
                         const DrawablePictureSource&) = default;
};

struct SolidPictureSource {
  PremultipliedColor color;
  friend bool operator==(const SolidPictureSource&,
                         const SolidPictureSource&) = default;
};

using PictureSource = std::variant<DrawablePictureSource, SolidPictureSource>;

struct PictureAttributes {
  std::int32_t clip_x_origin{};
  std::int32_t clip_y_origin{};
  std::optional<std::vector<geometry::Rectangle>> clip_rectangles;

  friend bool operator==(const PictureAttributes&,
                         const PictureAttributes&) = default;
};

struct PictureAttributeUpdate {
  std::optional<std::uint32_t> repeat;
  std::optional<std::uint32_t> alpha_map;
  std::optional<std::int32_t> clip_x_origin;
  std::optional<std::int32_t> clip_y_origin;
  std::optional<std::uint32_t> component_alpha;
  std::optional<std::uint32_t> subwindow_mode;
  std::uint32_t unsupported_mask{};
};

enum class PictureStatus {
  Success,
  UnknownFormat,
  DrawableFormatMismatch,
  InvalidPremultipliedColor,
  UnsupportedAttribute,
  BadAttributeValue,
  TooManyClipRectangles,
  InvalidClipRectangle,
  BadAlloc,
};

class Picture {
 public:
  static constexpr std::size_t kMaximumClipRectangles = 4096;

  [[nodiscard]] static std::optional<Picture> create_drawable(
      std::uint32_t drawable, PictureFormatId format, std::uint8_t depth,
      std::uint8_t bits_per_pixel) noexcept;
  [[nodiscard]] static std::optional<Picture> create_solid(
      PremultipliedColor color) noexcept;

  [[nodiscard]] PictureFormatId format() const noexcept { return format_; }
  [[nodiscard]] const PictureSource& source() const noexcept { return source_; }
  [[nodiscard]] const PictureAttributes& attributes() const noexcept {
    return attributes_;
  }

  [[nodiscard]] PictureStatus apply_attributes(
      const PictureAttributeUpdate& update) noexcept;
  [[nodiscard]] PictureStatus set_clip_rectangles(
      std::int32_t origin_x, std::int32_t origin_y,
      std::span<const geometry::Rectangle> rectangles) noexcept;
  void clear_clip() noexcept { attributes_.clip_rectangles.reset(); }

 private:
  Picture(PictureFormatId format, PictureSource source)
      : format_(format), source_(std::move(source)) {}

  PictureFormatId format_{};
  PictureSource source_;
  PictureAttributes attributes_;
};

}  // namespace glasswyrm::server
