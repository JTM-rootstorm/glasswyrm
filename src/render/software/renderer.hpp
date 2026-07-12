#pragma once

#include "compositor/rectangle.hpp"
#include "render/software/pixel.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace gw::render::software {

struct ImageView {
  std::span<const std::byte> bytes;
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t stride{};
  PixelFormat format{};
};

struct FramebufferView {
  std::span<std::byte> bytes;
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t stride{};
};

enum class RenderResult { Success, InvalidView, InvalidPremultipliedPixel };

[[nodiscard]] RenderResult clear(FramebufferView destination,
                                 compositor::Rectangle rectangle) noexcept;
[[nodiscard]] RenderResult composite(
    FramebufferView destination, const ImageView& source,
    compositor::Rectangle source_rectangle, std::int32_t destination_x,
    std::int32_t destination_y, std::uint32_t opacity) noexcept;

} // namespace gw::render::software
