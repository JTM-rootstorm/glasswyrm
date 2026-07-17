#include "glasswyrmd/render_ops.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace glasswyrm::server {
namespace {

using geometry::Rectangle;

std::uint32_t bytes_per_pixel(const RenderPixelFormat format) noexcept {
  return format == RenderPixelFormat::A1 || format == RenderPixelFormat::A8
             ? 1U
             : 4U;
}

template <typename View>
bool valid_surface(const View& view) noexcept {
  const auto row = static_cast<std::uint64_t>(view.width) *
                   bytes_per_pixel(view.format);
  const auto required = view.height == 0
                            ? std::uint64_t{0}
                            : static_cast<std::uint64_t>(view.height - 1U) *
                                      view.stride +
                                  row;
  return view.width != 0 && view.height != 0 && view.stride >= row &&
         required <= view.bytes.size();
}

std::uint32_t load_word(const std::byte* address) noexcept {
  std::uint32_t value{};
  std::memcpy(&value, address, sizeof(value));
  return value;
}

void store_word(std::byte* address, const std::uint32_t value) noexcept {
  std::memcpy(address, &value, sizeof(value));
}

PremultipliedColor load_pixel(const RenderSourceView& view,
                              const std::uint32_t x,
                              const std::uint32_t y) noexcept {
  const auto offset = static_cast<std::size_t>(y) * view.stride +
                      static_cast<std::size_t>(x) *
                          bytes_per_pixel(view.format);
  if (view.format == RenderPixelFormat::A1)
    return {0, 0, 0,
            static_cast<std::uint8_t>(view.bytes[offset] == std::byte{0}
                                          ? 0
                                          : 255)};
  if (view.format == RenderPixelFormat::A8)
    return {0, 0, 0, static_cast<std::uint8_t>(view.bytes[offset])};
  const auto value = load_word(view.bytes.data() + offset);
  return {static_cast<std::uint8_t>(value >> 16U),
          static_cast<std::uint8_t>(value >> 8U),
          static_cast<std::uint8_t>(value),
          view.format == RenderPixelFormat::Xrgb8888
              ? std::uint8_t{255}
              : static_cast<std::uint8_t>(value >> 24U)};
}

PremultipliedColor load_pixel(const RenderDestinationView& view,
                              const std::uint32_t x,
                              const std::uint32_t y) noexcept {
  const RenderSourceView source{view.format, view.width, view.height,
                                view.stride, view.bytes};
  return load_pixel(source, x, y);
}

void store_pixel(const RenderDestinationView& view, const std::uint32_t x,
                 const std::uint32_t y, PremultipliedColor pixel) noexcept {
  const auto offset = static_cast<std::size_t>(y) * view.stride +
                      static_cast<std::size_t>(x) *
                          bytes_per_pixel(view.format);
  if (view.format == RenderPixelFormat::A1) {
    view.bytes[offset] = pixel.alpha >= 128 ? std::byte{1} : std::byte{0};
    return;
  }
  if (view.format == RenderPixelFormat::A8) {
    view.bytes[offset] = static_cast<std::byte>(pixel.alpha);
    return;
  }
  if (view.format == RenderPixelFormat::Xrgb8888) pixel.alpha = 255;
  const auto value = (static_cast<std::uint32_t>(pixel.alpha) << 24U) |
                     (static_cast<std::uint32_t>(pixel.red) << 16U) |
                     (static_cast<std::uint32_t>(pixel.green) << 8U) |
                     pixel.blue;
  store_word(view.bytes.data() + offset, value);
}

bool premultiplied(const PremultipliedColor pixel) noexcept {
  return pixel.red <= pixel.alpha && pixel.green <= pixel.alpha &&
         pixel.blue <= pixel.alpha;
}

std::uint8_t over_channel(const std::uint8_t source,
                          const std::uint8_t destination,
                          const std::uint8_t source_alpha) noexcept {
  const auto value =
      static_cast<std::uint32_t>(source) +
      (static_cast<std::uint32_t>(destination) * (255U - source_alpha) +
       127U) /
          255U;
  return static_cast<std::uint8_t>(std::min(value, 255U));
}

PremultipliedColor apply_operator(const RenderOperator operation,
                                  const PremultipliedColor source,
                                  const PremultipliedColor destination) noexcept {
  if (operation == RenderOperator::Src) return source;
  return {over_channel(source.red, destination.red, source.alpha),
          over_channel(source.green, destination.green, source.alpha),
          over_channel(source.blue, destination.blue, source.alpha),
          over_channel(source.alpha, destination.alpha, source.alpha)};
}

bool contains(const Rectangle rectangle, const std::int32_t x,
              const std::int32_t y) noexcept {
  return x >= rectangle.x && y >= rectangle.y &&
         static_cast<std::int64_t>(x) <
             static_cast<std::int64_t>(rectangle.x) + rectangle.width &&
         static_cast<std::int64_t>(y) <
             static_cast<std::int64_t>(rectangle.y) + rectangle.height;
}

bool visible(const std::span<const Rectangle> clip, const std::int32_t x,
             const std::int32_t y) noexcept {
  return clip.empty() ||
         std::ranges::any_of(clip, [=](const auto rectangle) {
           return contains(rectangle, x, y);
         });
}

std::optional<Rectangle> union_damage(const std::optional<Rectangle> current,
                                      const Rectangle added) noexcept {
  if (added.empty()) return current;
  if (!current) return added;
  const auto left = std::min(current->x, added.x);
  const auto top = std::min(current->y, added.y);
  const auto right = std::max(
      static_cast<std::int64_t>(current->x) + current->width,
      static_cast<std::int64_t>(added.x) + added.width);
  const auto bottom = std::max(
      static_cast<std::int64_t>(current->y) + current->height,
      static_cast<std::int64_t>(added.y) + added.height);
  return Rectangle{left, top, static_cast<std::uint32_t>(right - left),
                   static_cast<std::uint32_t>(bottom - top)};
}

std::optional<Rectangle> clipped_damage(
    const Rectangle painted, const std::span<const Rectangle> clip) noexcept {
  if (clip.empty()) return painted;
  std::optional<Rectangle> result;
  for (const auto rectangle : clip)
    if (const auto overlap = geometry::intersect(painted, rectangle))
      result = union_damage(result, *overlap);
  return result;
}

struct Mapping {
  Rectangle destination;
  std::int64_t source_x{};
  std::int64_t source_y{};
};

std::optional<Mapping> clip_mapping(
    const RenderDestinationView& destination, const RenderSourceView& source,
    const std::int32_t source_x, const std::int32_t source_y,
    const std::int32_t destination_x, const std::int32_t destination_y,
    const std::uint32_t width, const std::uint32_t height) noexcept {
  const auto left = std::max(
      {std::int64_t{0}, -static_cast<std::int64_t>(source_x),
       -static_cast<std::int64_t>(destination_x)});
  const auto top = std::max(
      {std::int64_t{0}, -static_cast<std::int64_t>(source_y),
       -static_cast<std::int64_t>(destination_y)});
  const auto right = std::min(
      {static_cast<std::int64_t>(width),
       static_cast<std::int64_t>(source.width) - source_x,
       static_cast<std::int64_t>(destination.width) - destination_x});
  const auto bottom = std::min(
      {static_cast<std::int64_t>(height),
       static_cast<std::int64_t>(source.height) - source_y,
       static_cast<std::int64_t>(destination.height) - destination_y});
  if (right <= left || bottom <= top) return std::nullopt;
  return Mapping{{static_cast<std::int32_t>(
                      static_cast<std::int64_t>(destination_x) + left),
                  static_cast<std::int32_t>(
                      static_cast<std::int64_t>(destination_y) + top),
                  static_cast<std::uint32_t>(right - left),
                  static_cast<std::uint32_t>(bottom - top)},
                 static_cast<std::int64_t>(source_x) + left,
                 static_cast<std::int64_t>(source_y) + top};
}

}  // namespace

std::optional<PremultipliedColor> render_color_from_u16(
    const std::uint16_t red, const std::uint16_t green,
    const std::uint16_t blue, const std::uint16_t alpha) noexcept {
  if (red > alpha || green > alpha || blue > alpha) return std::nullopt;
  const auto narrow = [](const std::uint16_t value) {
    return static_cast<std::uint8_t>(
        (static_cast<std::uint32_t>(value) + 128U) / 257U);
  };
  return PremultipliedColor{narrow(red), narrow(green), narrow(blue),
                            narrow(alpha)};
}

RenderOpResult render_composite(
    const RenderDestinationView destination, const RenderSourceView source,
    const RenderOperator operation, const std::int32_t source_x,
    const std::int32_t source_y, const std::int32_t destination_x,
    const std::int32_t destination_y, const std::uint32_t width,
    const std::uint32_t height,
    const std::span<const Rectangle> destination_clip) {
  if (!valid_surface(destination) || !valid_surface(source))
    return {RenderOpStatus::InvalidSurface, {}};
  const auto mapping = clip_mapping(destination, source, source_x, source_y,
                                    destination_x, destination_y, width, height);
  if (!mapping) return {};
  const auto count = static_cast<std::uint64_t>(mapping->destination.width) *
                     mapping->destination.height;
  if (count > std::numeric_limits<std::size_t>::max() /
                  sizeof(PremultipliedColor))
    return {RenderOpStatus::BadAlloc, {}};
  try {
    std::vector<PremultipliedColor> sampled;
    sampled.reserve(static_cast<std::size_t>(count));
    for (std::uint32_t y = 0; y < mapping->destination.height; ++y)
      for (std::uint32_t x = 0; x < mapping->destination.width; ++x) {
        const auto source_pixel = load_pixel(
            source, static_cast<std::uint32_t>(mapping->source_x + x),
            static_cast<std::uint32_t>(mapping->source_y + y));
        if (!premultiplied(source_pixel))
          return {RenderOpStatus::InvalidPremultipliedPixel, {}};
        if (operation == RenderOperator::Over) {
          const auto destination_pixel = load_pixel(
              destination,
              static_cast<std::uint32_t>(mapping->destination.x) + x,
              static_cast<std::uint32_t>(mapping->destination.y) + y);
          if (!premultiplied(destination_pixel))
            return {RenderOpStatus::InvalidPremultipliedPixel, {}};
        }
        sampled.push_back(source_pixel);
      }
    std::size_t index = 0;
    for (std::uint32_t y = 0; y < mapping->destination.height; ++y)
      for (std::uint32_t x = 0; x < mapping->destination.width;
           ++x, ++index) {
        const auto target_x = mapping->destination.x +
                              static_cast<std::int32_t>(x);
        const auto target_y = mapping->destination.y +
                              static_cast<std::int32_t>(y);
        if (!visible(destination_clip, target_x, target_y)) continue;
        const auto destination_pixel = load_pixel(
            destination, static_cast<std::uint32_t>(target_x),
            static_cast<std::uint32_t>(target_y));
        store_pixel(destination, static_cast<std::uint32_t>(target_x),
                    static_cast<std::uint32_t>(target_y),
                    apply_operator(operation, sampled[index],
                                   destination_pixel));
      }
  } catch (const std::bad_alloc&) {
    return {RenderOpStatus::BadAlloc, {}};
  }
  return {RenderOpStatus::Success,
          clipped_damage(mapping->destination, destination_clip)};
}

RenderOpResult render_fill(
    const RenderDestinationView destination, const RenderOperator operation,
    const PremultipliedColor color,
    const std::span<const Rectangle> rectangles,
    const std::span<const Rectangle> destination_clip) {
  if (!valid_surface(destination))
    return {RenderOpStatus::InvalidSurface, {}};
  if (!premultiplied(color))
    return {RenderOpStatus::InvalidPremultipliedPixel, {}};
  const Rectangle bounds{0, 0, destination.width, destination.height};
  if (operation == RenderOperator::Over &&
      destination.format == RenderPixelFormat::Argb8888Premultiplied) {
    for (const auto rectangle : rectangles)
      if (const auto painted = geometry::intersect(rectangle, bounds))
        for (std::uint32_t y = 0; y < painted->height; ++y)
          for (std::uint32_t x = 0; x < painted->width; ++x) {
            const auto target_x = painted->x + static_cast<std::int32_t>(x);
            const auto target_y = painted->y + static_cast<std::int32_t>(y);
            if (visible(destination_clip, target_x, target_y) &&
                !premultiplied(load_pixel(
                    destination, static_cast<std::uint32_t>(target_x),
                    static_cast<std::uint32_t>(target_y))))
              return {RenderOpStatus::InvalidPremultipliedPixel, {}};
          }
  }
  std::optional<Rectangle> damage;
  for (const auto rectangle : rectangles) {
    const auto painted = geometry::intersect(rectangle, bounds);
    if (!painted) continue;
    damage = union_damage(damage, *painted);
    for (std::uint32_t y = 0; y < painted->height; ++y)
      for (std::uint32_t x = 0; x < painted->width; ++x) {
        const auto target_x = painted->x + static_cast<std::int32_t>(x);
        const auto target_y = painted->y + static_cast<std::int32_t>(y);
        if (!visible(destination_clip, target_x, target_y)) continue;
        const auto existing = load_pixel(
            destination, static_cast<std::uint32_t>(target_x),
            static_cast<std::uint32_t>(target_y));
        store_pixel(destination, static_cast<std::uint32_t>(target_x),
                    static_cast<std::uint32_t>(target_y),
                    apply_operator(operation, color, existing));
      }
  }
  return {RenderOpStatus::Success,
          damage ? clipped_damage(*damage, destination_clip) : std::nullopt};
}

}  // namespace glasswyrm::server
