#include "render/software/renderer.hpp"

#include "render/software/blend.hpp"

#include <limits>

namespace gw::render::software {
namespace {

[[nodiscard]] bool valid_image(const ImageView& image) noexcept {
  const std::uint64_t row = static_cast<std::uint64_t>(image.width) * 4U;
  const std::uint64_t required = image.height == 0
      ? 0
      : static_cast<std::uint64_t>(image.height - 1U) * image.stride + row;
  return image.width != 0 && image.height != 0 && image.stride >= row &&
         required <= image.bytes.size();
}

[[nodiscard]] bool valid_framebuffer(const FramebufferView& image) noexcept {
  const std::uint64_t row = static_cast<std::uint64_t>(image.width) * 4U;
  const std::uint64_t required = image.height == 0
      ? 0
      : static_cast<std::uint64_t>(image.height - 1U) * image.stride + row;
  return image.width != 0 && image.height != 0 && image.stride >= row &&
         required <= image.bytes.size();
}

} // namespace

RenderResult clear(const FramebufferView destination,
                   const compositor::Rectangle rectangle) noexcept {
  if (!valid_framebuffer(destination)) return RenderResult::InvalidView;
  const compositor::Rectangle bounds{0, 0, destination.width, destination.height};
  const auto clipped = compositor::intersection(rectangle, bounds);
  if (!clipped) return RenderResult::Success;
  for (std::uint32_t y = 0; y < clipped->height; ++y) {
    for (std::uint32_t x = 0; x < clipped->width; ++x) {
      const auto offset = static_cast<std::size_t>(clipped->y + y) * destination.stride +
                          static_cast<std::size_t>(clipped->x + x) * 4U;
      store_u32(destination.bytes.data() + offset, 0xff000000U);
    }
  }
  return RenderResult::Success;
}

RenderResult composite(const FramebufferView destination, const ImageView& source,
                       const compositor::Rectangle source_rectangle,
                       const std::int32_t destination_x,
                       const std::int32_t destination_y,
                       const std::uint32_t opacity) noexcept {
  if (!valid_framebuffer(destination) || !valid_image(source) ||
      !compositor::has_valid_extents(source_rectangle)) {
    return RenderResult::InvalidView;
  }
  const compositor::Rectangle source_bounds{0, 0, source.width, source.height};
  const auto sampled = compositor::intersection(source_rectangle, source_bounds);
  if (!sampled) return RenderResult::Success;
  const auto translated = compositor::translate(*sampled, destination_x, destination_y);
  if (!translated) return RenderResult::InvalidView;
  const compositor::Rectangle destination_bounds{0, 0, destination.width,
                                                  destination.height};
  const auto painted = compositor::intersection(*translated, destination_bounds);
  if (!painted) return RenderResult::Success;

  const std::int64_t source_start_x =
      static_cast<std::int64_t>(sampled->x) + painted->x - translated->x;
  const std::int64_t source_start_y =
      static_cast<std::int64_t>(sampled->y) + painted->y - translated->y;
  if (source.format == PixelFormat::Argb8888Premultiplied) {
    for (std::uint32_t y = 0; y < painted->height; ++y) {
      for (std::uint32_t x = 0; x < painted->width; ++x) {
        const auto source_offset =
            static_cast<std::size_t>(source_start_y + y) * source.stride +
            static_cast<std::size_t>(source_start_x + x) * 4U;
        if (!is_premultiplied(
                unpack_argb8888(load_u32(source.bytes.data() + source_offset)))) {
          return RenderResult::InvalidPremultipliedPixel;
        }
      }
    }
  }
  for (std::uint32_t y = 0; y < painted->height; ++y) {
    for (std::uint32_t x = 0; x < painted->width; ++x) {
      const auto source_offset = static_cast<std::size_t>(source_start_y + y) * source.stride +
                                 static_cast<std::size_t>(source_start_x + x) * 4U;
      const auto destination_offset = static_cast<std::size_t>(painted->y + y) * destination.stride +
                                      static_cast<std::size_t>(painted->x + x) * 4U;
      const std::uint32_t source_word = load_u32(source.bytes.data() + source_offset);
      const Pixel source_pixel = source.format == PixelFormat::Xrgb8888
          ? unpack_xrgb8888(source_word)
          : unpack_argb8888(source_word);
      const Pixel destination_pixel =
          unpack_xrgb8888(load_u32(destination.bytes.data() + destination_offset));
      store_u32(destination.bytes.data() + destination_offset,
                pack_xrgb8888(blend(source_pixel, destination_pixel, opacity)));
    }
  }
  return RenderResult::Success;
}

} // namespace gw::render::software
