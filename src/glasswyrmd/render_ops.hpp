#pragma once

#include "core/geometry/rectangle.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace glasswyrm::server {

enum class RenderPixelFormat {
  A1,
  A8,
  Xrgb8888,
  Argb8888Premultiplied,
};

enum class RenderOperator { Src, Over };

struct PremultipliedColor {
  std::uint8_t red{};
  std::uint8_t green{};
  std::uint8_t blue{};
  std::uint8_t alpha{};

  friend bool operator==(const PremultipliedColor&,
                         const PremultipliedColor&) = default;
};

struct RenderSourceView {
  RenderPixelFormat format{RenderPixelFormat::Xrgb8888};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t stride{};
  std::span<const std::byte> bytes;
};

struct RenderDestinationView {
  RenderPixelFormat format{RenderPixelFormat::Xrgb8888};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t stride{};
  std::span<std::byte> bytes;
};

enum class RenderOpStatus {
  Success,
  InvalidSurface,
  InvalidPremultipliedPixel,
  BadAlloc,
};

struct RenderOpResult {
  RenderOpStatus status{RenderOpStatus::Success};
  std::optional<geometry::Rectangle> damage;
};

[[nodiscard]] std::optional<PremultipliedColor> render_color_from_u16(
    std::uint16_t red, std::uint16_t green, std::uint16_t blue,
    std::uint16_t alpha) noexcept;

[[nodiscard]] RenderOpResult render_composite(
    RenderDestinationView destination, RenderSourceView source,
    RenderOperator operation, std::int32_t source_x, std::int32_t source_y,
    std::int32_t destination_x, std::int32_t destination_y,
    std::uint32_t width, std::uint32_t height,
    std::span<const geometry::Rectangle> destination_clip = {});

[[nodiscard]] RenderOpResult render_fill(
    RenderDestinationView destination, RenderOperator operation,
    PremultipliedColor color,
    std::span<const geometry::Rectangle> rectangles,
    std::span<const geometry::Rectangle> destination_clip = {});

}  // namespace glasswyrm::server
