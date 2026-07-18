#include "glasswyrmd/extensions/render_internal.hpp"

#include "glasswyrmd/picture.hpp"
#include "glasswyrmd/request_handlers/common.hpp"
#include "glasswyrmd/request_handlers/drawable_access.hpp"
#include "glasswyrmd/render_ops.hpp"
#include "protocol/x11/byte_cursor.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace glasswyrm::server::extensions {
namespace x11 = gw::protocol::x11;
using request_handlers::error;

namespace {

std::optional<RenderPixelFormat> pixel_format(const PictureFormatId format) {
  switch (format) {
    case PictureFormatId::A1: return RenderPixelFormat::A1;
    case PictureFormatId::A8: return RenderPixelFormat::A8;
    case PictureFormatId::Xrgb32: return RenderPixelFormat::Xrgb8888;
    case PictureFormatId::Argb32:
      return RenderPixelFormat::Argb8888Premultiplied;
  }
  return std::nullopt;
}

std::optional<RenderDestinationView> destination_view(ResourceTable& resources,
                                                       const Picture& picture) {
  const auto* source = std::get_if<DrawablePictureSource>(&picture.source());
  const auto format = pixel_format(picture.format());
  if (!source || !format) return std::nullopt;
  if (auto* pixmap = resources.find_pixmap(source->drawable)) {
    if (auto* bitmap = pixmap->bitmap())
      return RenderDestinationView{
          *format, pixmap->width, pixmap->height, pixmap->width,
          std::as_writable_bytes(bitmap->bits())};
    if (auto* alpha = pixmap->alpha())
      return RenderDestinationView{*format, pixmap->width, pixmap->height,
                                   alpha->stride(),
                                   std::as_writable_bytes(alpha->bytes())};
    if (auto* pixels = pixmap->pixels())
      return RenderDestinationView{*format, pixmap->width, pixmap->height,
                                   pixels->stride(),
                                   std::as_writable_bytes(pixels->pixels())};
    return std::nullopt;
  }
  auto* pixels = request_handlers::mutable_storage(resources, source->drawable);
  if (!pixels) return std::nullopt;
  return RenderDestinationView{*format, pixels->width(), pixels->height(),
                               pixels->stride(),
                               std::as_writable_bytes(pixels->pixels())};
}

RenderSourceView source_view(const RenderDestinationView view) {
  return {view.format, view.width, view.height, view.stride, view.bytes};
}

struct ClipResult {
  bool constrained{};
  std::vector<geometry::Rectangle> rectangles;
};

std::optional<geometry::Rectangle> bounded_rectangle(
    const std::int64_t left, const std::int64_t top, const std::int64_t right,
    const std::int64_t bottom, const std::uint32_t width,
    const std::uint32_t height) {
  const auto clipped_left = std::max(left, std::int64_t{0});
  const auto clipped_top = std::max(top, std::int64_t{0});
  const auto clipped_right = std::min(right, static_cast<std::int64_t>(width));
  const auto clipped_bottom =
      std::min(bottom, static_cast<std::int64_t>(height));
  if (clipped_right <= clipped_left || clipped_bottom <= clipped_top)
    return std::nullopt;
  return geometry::Rectangle{
      static_cast<std::int32_t>(clipped_left),
      static_cast<std::int32_t>(clipped_top),
      static_cast<std::uint32_t>(clipped_right - clipped_left),
      static_cast<std::uint32_t>(clipped_bottom - clipped_top)};
}

ClipResult translated_clip(const Picture& picture, const std::int64_t shift_x,
                           const std::int64_t shift_y,
                           const RenderDestinationView destination) {
  ClipResult result;
  const auto& attributes = picture.attributes();
  if (!attributes.clip_rectangles) return result;
  result.constrained = true;
  result.rectangles.reserve(attributes.clip_rectangles->size());
  for (const auto rectangle : *attributes.clip_rectangles) {
    const auto left = static_cast<std::int64_t>(attributes.clip_x_origin) +
                      rectangle.x + shift_x;
    const auto top = static_cast<std::int64_t>(attributes.clip_y_origin) +
                     rectangle.y + shift_y;
    const auto clipped = bounded_rectangle(
        left, top, left + rectangle.width, top + rectangle.height,
        destination.width, destination.height);
    if (clipped) result.rectangles.push_back(*clipped);
  }
  return result;
}

ClipResult combine_clips(ClipResult left, ClipResult right) {
  if (!left.constrained) return right;
  if (!right.constrained) return left;
  ClipResult result{true, {}};
  result.rectangles.reserve(left.rectangles.size() * right.rectangles.size());
  for (const auto first : left.rectangles)
    for (const auto second : right.rectangles)
      if (const auto overlap = geometry::intersect(first, second))
        result.rectangles.push_back(*overlap);
  return result;
}

std::optional<RenderOperator> render_operator(const std::uint8_t operation) {
  if (operation == 1) return RenderOperator::Src;
  if (operation == 3) return RenderOperator::Over;
  return std::nullopt;
}

DispatchResult raster_status(const RenderOpResult& result,
                             const DispatchContext& context,
                             const x11::FramedRequest& request,
                             const std::uint32_t drawable) {
  switch (result.status) {
    case RenderOpStatus::Success: return {};
    case RenderOpStatus::InvalidSurface:
      return error(context, request, x11::CoreErrorCode::BadMatch, drawable);
    case RenderOpStatus::InvalidPremultipliedPixel:
      return error(context, request, x11::CoreErrorCode::BadValue);
    case RenderOpStatus::BadAlloc:
      return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return {};
}

DispatchResult finish_raster(const RenderOpResult& raster, ServerState& state,
                             const DispatchContext& context,
                             const x11::FramedRequest& request,
                             const std::uint32_t drawable) {
  if (raster.status != RenderOpStatus::Success)
    return raster_status(raster, context, request, drawable);
  DispatchResult result;
  if (raster.damage)
    request_handlers::add_drawable_damage(
        result, state, drawable, *raster.damage, context.input.logical_time);
  return result;
}

DispatchResult composite(ServerState& state, const DispatchContext& context,
                         const x11::FramedRequest& request) {
  if (request.core_size() != 36)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint8_t raw_operator{};
  std::uint32_t source_xid{}, mask_xid{}, destination_xid{};
  std::uint16_t raw_source_x{}, raw_source_y{}, ignored{}, raw_destination_x{},
      raw_destination_y{}, width{}, height{};
  (void)reader.read_u8(raw_operator);
  (void)reader.skip(3);
  (void)reader.read_u32(source_xid);
  (void)reader.read_u32(mask_xid);
  (void)reader.read_u32(destination_xid);
  (void)reader.read_u16(raw_source_x);
  (void)reader.read_u16(raw_source_y);
  (void)reader.read_u16(ignored);
  (void)reader.read_u16(ignored);
  (void)reader.read_u16(raw_destination_x);
  (void)reader.read_u16(raw_destination_y);
  (void)reader.read_u16(width);
  (void)reader.read_u16(height);
  const auto operation = render_operator(raw_operator);
  if (!operation)
    return render_extension_error(context, request, 2, raw_operator);
  const auto* source = state.resources().find_picture(source_xid);
  if (!source) return render_extension_error(context, request, 1, source_xid);
  const auto* destination = state.resources().find_picture(destination_xid);
  if (!destination)
    return render_extension_error(context, request, 1, destination_xid);
  if (mask_xid != 0)
    return error(context, request, x11::CoreErrorCode::BadMatch, mask_xid);
  const auto destination_drawable =
      std::get_if<DrawablePictureSource>(&destination->source());
  auto destination_surface = destination_view(state.resources(), *destination);
  if (!destination_drawable || !destination_surface)
    return error(context, request, x11::CoreErrorCode::BadMatch,
                 destination_xid);
  const auto source_x = std::bit_cast<std::int16_t>(raw_source_x);
  const auto source_y = std::bit_cast<std::int16_t>(raw_source_y);
  const auto destination_x =
      std::bit_cast<std::int16_t>(raw_destination_x);
  const auto destination_y =
      std::bit_cast<std::int16_t>(raw_destination_y);
  auto clip = combine_clips(
      translated_clip(*destination, 0, 0, *destination_surface),
      translated_clip(*source,
                      static_cast<std::int64_t>(destination_x) - source_x,
                      static_cast<std::int64_t>(destination_y) - source_y,
                      *destination_surface));
  if (clip.constrained && clip.rectangles.empty()) return {};
  const auto clip_span = std::span<const geometry::Rectangle>{clip.rectangles};
  RenderOpResult raster;
  if (const auto* solid = std::get_if<SolidPictureSource>(&source->source())) {
    const geometry::Rectangle rectangle{destination_x, destination_y, width,
                                        height};
    raster = render_fill(*destination_surface, *operation, solid->color,
                         std::span{&rectangle, std::size_t{1}}, clip_span);
  } else {
    auto source_surface = destination_view(state.resources(), *source);
    if (!source_surface)
      return error(context, request, x11::CoreErrorCode::BadMatch, source_xid);
    raster = render_composite(*destination_surface, source_view(*source_surface),
                              *operation, source_x, source_y, destination_x,
                              destination_y, width, height, clip_span);
  }
  return finish_raster(raster, state, context, request,
                       destination_drawable->drawable);
}

DispatchResult fill_rectangles(ServerState& state,
                               const DispatchContext& context,
                               const x11::FramedRequest& request) {
  if (request.core_size() < 20 || (request.core_size() - 20) % 8 != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  if ((request.core_size() - 20) / 8 > Picture::kMaximumClipRectangles)
    return error(context, request, x11::CoreErrorCode::BadAlloc);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint8_t raw_operator{};
  std::uint32_t destination_xid{};
  std::uint16_t red{}, green{}, blue{}, alpha{};
  (void)reader.read_u8(raw_operator);
  (void)reader.skip(3);
  (void)reader.read_u32(destination_xid);
  (void)reader.read_u16(red);
  (void)reader.read_u16(green);
  (void)reader.read_u16(blue);
  (void)reader.read_u16(alpha);
  const auto operation = render_operator(raw_operator);
  if (!operation)
    return render_extension_error(context, request, 2, raw_operator);
  const auto color = render_color_from_u16(red, green, blue, alpha);
  if (!color) return error(context, request, x11::CoreErrorCode::BadValue);
  const auto* destination = state.resources().find_picture(destination_xid);
  if (!destination)
    return render_extension_error(context, request, 1, destination_xid);
  const auto* destination_drawable =
      std::get_if<DrawablePictureSource>(&destination->source());
  auto destination_surface = destination_view(state.resources(), *destination);
  if (!destination_drawable || !destination_surface)
    return error(context, request, x11::CoreErrorCode::BadMatch,
                 destination_xid);
  std::vector<geometry::Rectangle> rectangles;
  try {
    rectangles.reserve((request.core_size() - 20) / 8);
    while (reader.remaining() != 0) {
      std::uint16_t raw_x{}, raw_y{}, width{}, height{};
      (void)reader.read_u16(raw_x);
      (void)reader.read_u16(raw_y);
      (void)reader.read_u16(width);
      (void)reader.read_u16(height);
      rectangles.push_back({std::bit_cast<std::int16_t>(raw_x),
                            std::bit_cast<std::int16_t>(raw_y), width, height});
    }
    auto clip = translated_clip(*destination, 0, 0, *destination_surface);
    if (clip.constrained && clip.rectangles.empty()) return {};
    const auto raster = render_fill(
        *destination_surface, *operation, *color, rectangles, clip.rectangles);
    return finish_raster(raster, state, context, request,
                         destination_drawable->drawable);
  } catch (const std::bad_alloc&) {
    return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
}

}  // namespace

DispatchResult render_raster_request(ServerState& state,
                                     const DispatchContext& context,
                                     const x11::FramedRequest& request) {
  try {
    if (request.data == 8) return composite(state, context, request);
    if (request.data == 26) return fill_rectangles(state, context, request);
  } catch (const std::bad_alloc&) {
    return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return error(context, request, x11::CoreErrorCode::BadRequest);
}

}  // namespace glasswyrm::server::extensions
