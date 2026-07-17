#include "glasswyrmd/request_handlers/common.hpp"

#include "glasswyrmd/request_handlers/drawable_access.hpp"
#include "glasswyrmd/m9_raster_ops.hpp"
#include "glasswyrmd/raster_ops.hpp"
#include "core/geometry/region.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/exposure_event.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace glasswyrm::server::request_handlers {
namespace x11 = gw::protocol::x11;

void fill_opaque_stippled(PixelStorage& destination,
                          const geometry::Rectangle rectangle,
                          const GraphicsContextResource& gc) noexcept {
  const auto clipped = geometry::intersect(
      rectangle, {0, 0, destination.width(), destination.height()});
  if (!clipped) return;
  const auto mask = gc.plane_mask & 0x00ffffffU;
  for (std::uint32_t y = 0; y < clipped->height; ++y)
    for (std::uint32_t x = 0; x < clipped->width; ++x) {
      const auto destination_x = static_cast<std::uint32_t>(clipped->x) + x;
      const auto destination_y = static_cast<std::uint32_t>(clipped->y) + y;
      const bool foreground = !gc.stipple ||
          gc.stipple->at(destination_x % gc.stipple->width(),
                         destination_y % gc.stipple->height()) != 0;
      const auto source = foreground ? gc.foreground : gc.background;
      auto& pixel = destination.at(destination_x, destination_y);
      pixel = 0xff000000U | (source & mask) |
              (pixel & ~mask & 0x00ffffffU);
    }
}

std::optional<geometry::Rectangle> clipped_bounds(
    const PixelStorage& storage, const std::span<const RasterPoint> points) {
  if (points.empty()) return std::nullopt;
  auto minimum_x = points.front().x, maximum_x = points.front().x;
  auto minimum_y = points.front().y, maximum_y = points.front().y;
  for (const auto point : points) {
    minimum_x = std::min(minimum_x, point.x); maximum_x = std::max(maximum_x, point.x);
    minimum_y = std::min(minimum_y, point.y); maximum_y = std::max(maximum_y, point.y);
  }
  return geometry::intersect(
      {minimum_x, minimum_y,
       static_cast<std::uint32_t>(static_cast<std::int64_t>(maximum_x) - minimum_x + 1),
       static_cast<std::uint32_t>(static_cast<std::int64_t>(maximum_y) - minimum_y + 1)},
      {0, 0, storage.width(), storage.height()});
}

DispatchResult poly_line(ServerState& state, const DispatchContext& context,
                         const x11::FramedRequest& request) {
  if (request.bytes.size() < 12 || (request.bytes.size() - 12U) % 4U != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (request.data > 1)
    return error(context, request, x11::CoreErrorCode::BadValue, request.data);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t drawable{}, gc_id{}; (void)reader.read_u32(drawable); (void)reader.read_u32(gc_id);
  const auto* gc = state.resources().find_gc(gc_id);
  if (!gc) return error(context, request, x11::CoreErrorCode::BadGContext, gc_id);
  if (gc->fill_style != 0)
    return error(context, request, x11::CoreErrorCode::BadImplementation);
  if (!state.resources().find_pixmap(drawable) && !supported_window_drawable(state.resources(), drawable))
    return error(context, request, known_drawable(state.resources(), drawable)
        ? x11::CoreErrorCode::BadMatch : x11::CoreErrorCode::BadDrawable, drawable);
  std::vector<RasterPoint> points;
  points.reserve((request.bytes.size() - 12U) / 4U);
  while (reader.remaining() != 0) {
    std::uint16_t raw_x{}, raw_y{}; (void)reader.read_u16(raw_x); (void)reader.read_u16(raw_y);
    RasterPoint point{static_cast<std::int16_t>(raw_x), static_cast<std::int16_t>(raw_y)};
    if (request.data == 1 && !points.empty()) {
      const auto x = static_cast<std::int64_t>(points.back().x) + point.x;
      const auto y = static_cast<std::int64_t>(points.back().y) + point.y;
      if (x < std::numeric_limits<std::int32_t>::min() || x > std::numeric_limits<std::int32_t>::max() ||
          y < std::numeric_limits<std::int32_t>::min() || y > std::numeric_limits<std::int32_t>::max())
        return error(context, request, x11::CoreErrorCode::BadValue);
      point = {static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)};
    }
    points.push_back(point);
  }
  auto* storage = mutable_storage(state.resources(), drawable);
  if (!storage) return error(context, request, x11::CoreErrorCode::BadAlloc);
  ClipByChildrenGuard child_clip(state.resources(), drawable, *gc, *storage);
  if (points.size() == 1) draw_line(*storage, points[0], points[0], gc->foreground, gc->plane_mask);
  else for (std::size_t index = 1; index < points.size(); ++index)
    draw_line(*storage, points[index - 1], points[index], gc->foreground, gc->plane_mask);
  child_clip.restore(); DispatchResult result;
  if (supported_window_drawable(state.resources(), drawable))
    if (const auto damage = clipped_bounds(*storage, points))
      for (const auto rectangle : child_clip.visible(*damage)) add_window_damage(result, state.resources(), drawable, rectangle);
  return result;
}

DispatchResult poly_segment(ServerState& state, const DispatchContext& context,
                            const x11::FramedRequest& request) {
  if (request.bytes.size() < 12 || (request.bytes.size() - 12U) % 8U != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t drawable{}, gc_id{}; (void)reader.read_u32(drawable); (void)reader.read_u32(gc_id);
  const auto* gc = state.resources().find_gc(gc_id);
  if (!gc) return error(context, request, x11::CoreErrorCode::BadGContext, gc_id);
  if (gc->fill_style != 0)
    return error(context, request, x11::CoreErrorCode::BadImplementation);
  if (!state.resources().find_pixmap(drawable) && !supported_window_drawable(state.resources(), drawable))
    return error(context, request, known_drawable(state.resources(), drawable)
        ? x11::CoreErrorCode::BadMatch : x11::CoreErrorCode::BadDrawable, drawable);
  std::vector<RasterSegment> segments;
  std::vector<RasterPoint> damage_points;
  while (reader.remaining() != 0) {
    std::uint16_t x1{}, y1{}, x2{}, y2{};
    (void)reader.read_u16(x1); (void)reader.read_u16(y1); (void)reader.read_u16(x2); (void)reader.read_u16(y2);
    RasterSegment segment{{static_cast<std::int16_t>(x1), static_cast<std::int16_t>(y1)},
                          {static_cast<std::int16_t>(x2), static_cast<std::int16_t>(y2)}};
    segments.push_back(segment); damage_points.push_back(segment.first); damage_points.push_back(segment.second);
  }
  auto* storage = mutable_storage(state.resources(), drawable);
  if (!storage) return error(context, request, x11::CoreErrorCode::BadAlloc);
  ClipByChildrenGuard child_clip(state.resources(), drawable, *gc, *storage);
  draw_segments(*storage, segments, gc->foreground, gc->plane_mask);
  child_clip.restore(); DispatchResult result;
  if (supported_window_drawable(state.resources(), drawable))
    if (const auto damage = clipped_bounds(*storage, damage_points))
      for (const auto rectangle : child_clip.visible(*damage)) add_window_damage(result, state.resources(), drawable, rectangle);
  return result;
}

DispatchResult fill_poly(ServerState& state, const DispatchContext& context,
                         const x11::FramedRequest& request) {
  if (request.bytes.size() < 16 || (request.bytes.size() - 16U) % 4U != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t drawable{}, gc_id{}; std::uint8_t shape{}, mode{};
  (void)reader.read_u32(drawable); (void)reader.read_u32(gc_id);
  (void)reader.read_u8(shape); (void)reader.read_u8(mode); (void)reader.skip(2);
  if (shape > 2) return error(context, request, x11::CoreErrorCode::BadValue, shape);
  if (mode > 1) return error(context, request, x11::CoreErrorCode::BadValue, mode);
  if (shape != 2 || mode != 0) return error(context, request, x11::CoreErrorCode::BadImplementation);
  const auto* gc = state.resources().find_gc(gc_id);
  if (!gc) return error(context, request, x11::CoreErrorCode::BadGContext, gc_id);
  if (gc->fill_style != 0)
    return error(context, request, x11::CoreErrorCode::BadImplementation);
  if (!state.resources().find_pixmap(drawable) && !supported_window_drawable(state.resources(), drawable))
    return error(context, request, known_drawable(state.resources(), drawable)
        ? x11::CoreErrorCode::BadMatch : x11::CoreErrorCode::BadDrawable, drawable);
  std::vector<RasterPoint> points;
  while (reader.remaining() != 0) {
    std::uint16_t x{}, y{}; (void)reader.read_u16(x); (void)reader.read_u16(y);
    points.push_back({static_cast<std::int16_t>(x), static_cast<std::int16_t>(y)});
  }
  auto* storage = mutable_storage(state.resources(), drawable);
  if (!storage) return error(context, request, x11::CoreErrorCode::BadAlloc);
  ClipByChildrenGuard child_clip(state.resources(), drawable, *gc, *storage);
  fill_convex_polygon(*storage, points, gc->foreground, gc->plane_mask);
  child_clip.restore(); DispatchResult result;
  if (supported_window_drawable(state.resources(), drawable))
    if (const auto damage = clipped_bounds(*storage, points))
      for (const auto rectangle : child_clip.visible(*damage)) add_window_damage(result, state.resources(), drawable, rectangle);
  return result;
}

DispatchResult poly_fill_arc(ServerState& state, const DispatchContext& context,
                             const x11::FramedRequest& request) {
  if (request.bytes.size() < 12 || (request.bytes.size() - 12U) % 12U != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t drawable{}, gc_id{}; (void)reader.read_u32(drawable); (void)reader.read_u32(gc_id);
  const auto* gc = state.resources().find_gc(gc_id);
  if (!gc) return error(context, request, x11::CoreErrorCode::BadGContext, gc_id);
  if (gc->fill_style != 0)
    return error(context, request, x11::CoreErrorCode::BadImplementation);
  if (!state.resources().find_pixmap(drawable) && !supported_window_drawable(state.resources(), drawable))
    return error(context, request, known_drawable(state.resources(), drawable)
        ? x11::CoreErrorCode::BadMatch : x11::CoreErrorCode::BadDrawable, drawable);
  std::vector<RasterEllipse> ellipses;
  while (reader.remaining() != 0) {
    std::uint16_t x{}, y{}, width{}, height{}, angle1{}, angle2{};
    (void)reader.read_u16(x); (void)reader.read_u16(y); (void)reader.read_u16(width); (void)reader.read_u16(height);
    (void)reader.read_u16(angle1); (void)reader.read_u16(angle2);
    static_cast<void>(angle1);
    const auto extent = static_cast<std::int16_t>(angle2);
    if (extent != 360 * 64 && extent != -360 * 64)
      return error(context, request, x11::CoreErrorCode::BadImplementation);
    ellipses.push_back({static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), width, height});
  }
  auto* storage = mutable_storage(state.resources(), drawable);
  if (!storage) return error(context, request, x11::CoreErrorCode::BadAlloc);
  ClipByChildrenGuard child_clip(state.resources(), drawable, *gc, *storage);
  geometry::Region damage({0, 0, storage->width(), storage->height()});
  for (const auto ellipse : ellipses) {
    fill_ellipse(*storage, ellipse, gc->foreground, gc->plane_mask);
    damage.add({ellipse.x, ellipse.y, ellipse.width, ellipse.height});
  }
  child_clip.restore(); DispatchResult result;
  if (supported_window_drawable(state.resources(), drawable))
    for (const auto& rectangle : damage.rectangles())
      for (const auto visible : child_clip.visible(rectangle)) add_window_damage(result, state.resources(), drawable, visible);
  return result;
}

DispatchResult put_image(ServerState& state, const DispatchContext& context,
                         const x11::FramedRequest& request) {
  if (request.bytes.size() < 24) return error(context, request, x11::CoreErrorCode::BadLength);
  if (request.data > 2) return error(context, request, x11::CoreErrorCode::BadValue, request.data);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t drawable{}, gc_id{}; std::uint16_t width{}, height{}, raw_x{}, raw_y{}; std::uint8_t left_pad{}, depth{};
  (void)reader.read_u32(drawable); (void)reader.read_u32(gc_id); (void)reader.read_u16(width);
  (void)reader.read_u16(height); (void)reader.read_u16(raw_x); (void)reader.read_u16(raw_y);
  (void)reader.read_u8(left_pad); (void)reader.read_u8(depth); (void)reader.skip(2);
  const bool bitmap_payload = request.data <= 1 || depth == 1;
  const auto payload_size = bitmap_payload
      ? ((static_cast<std::uint64_t>(width) + 31U) / 32U * 4U) * height
      : static_cast<std::uint64_t>(width) * height * 4U;
  if (payload_size > std::numeric_limits<std::size_t>::max() ||
      request.bytes.size() != 24U + payload_size)
    return error(context, request, x11::CoreErrorCode::BadLength);
  const auto expected_depth = request.data <= 1 ? 1U : depth;
  if ((request.data <= 1 && depth != 1) ||
      (request.data == 2 && depth != 1 && depth != 24) || left_pad != 0)
    return error(context, request, x11::CoreErrorCode::BadValue,
                 ((request.data <= 1 && depth != 1) ||
                  (request.data == 2 && depth != 1 && depth != 24))
                     ? depth
                     : left_pad);
  auto* gc = state.resources().find_gc(gc_id);
  if (!gc) return error(context, request, x11::CoreErrorCode::BadGContext, gc_id);
  auto* pixmap = state.resources().find_pixmap(drawable);
  const bool valid = pixmap || supported_window_drawable(state.resources(), drawable);
  if (!valid) return error(context, request, known_drawable(state.resources(), drawable)
      ? x11::CoreErrorCode::BadMatch : x11::CoreErrorCode::BadDrawable, drawable);
  if (gc->depth != expected_depth || (pixmap && pixmap->depth != expected_depth) ||
      (!pixmap && expected_depth != 24))
    return error(context, request, x11::CoreErrorCode::BadMatch, drawable);
  const auto payload = std::span<const std::uint8_t>(request.bytes).subspan(24);
  if (request.data <= 1) {
    auto* bitmap = pixmap ? pixmap->bitmap() : nullptr;
    if (!bitmap) return error(context, request, x11::CoreErrorCode::BadMatch, drawable);
    return put_xybitmap_lsb32(*bitmap, static_cast<std::int16_t>(raw_x),
                             static_cast<std::int16_t>(raw_y), width, height,
                             payload, gc->foreground, gc->background,
                             gc->plane_mask)
        ? DispatchResult{}
        : error(context, request, x11::CoreErrorCode::BadLength);
  }
  if (depth == 1) {
    auto* bitmap = pixmap ? pixmap->bitmap() : nullptr;
    if (!bitmap) return error(context, request, x11::CoreErrorCode::BadMatch, drawable);
    return put_zpixmap_lsb32(*bitmap, static_cast<std::int16_t>(raw_x),
                            static_cast<std::int16_t>(raw_y), width, height,
                            payload, gc->plane_mask)
        ? DispatchResult{}
        : error(context, request, x11::CoreErrorCode::BadLength);
  }
  auto* storage = mutable_storage(state.resources(), drawable);
  if (!storage) return error(context, request, x11::CoreErrorCode::BadAlloc);
  ClipByChildrenGuard child_clip(state.resources(), drawable, *gc, *storage);
  const auto raster = put_zpixmap(*storage, static_cast<std::int16_t>(raw_x),
      static_cast<std::int16_t>(raw_y), width, height, payload, gc->plane_mask);
  if (!raster.success) return error(context, request, x11::CoreErrorCode::BadLength);
  child_clip.restore(); DispatchResult result;
  if (!raster.damage.empty() && supported_window_drawable(state.resources(), drawable))
    for (const auto rectangle : child_clip.visible(raster.damage)) add_window_damage(result, state.resources(), drawable, rectangle);
  return result;
}

DispatchResult poly_fill_rectangle(ServerState& state, const DispatchContext& context,
                                   const x11::FramedRequest& request) {
  if (request.bytes.size() < 12 || (request.bytes.size() - 12U) % 8U != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order); std::uint32_t drawable{}, gc_id{};
  (void)reader.read_u32(drawable); (void)reader.read_u32(gc_id);
  auto* gc = state.resources().find_gc(gc_id);
  if (!gc) return error(context, request, x11::CoreErrorCode::BadGContext, gc_id);
  const bool valid = state.resources().find_pixmap(drawable) || supported_window_drawable(state.resources(), drawable);
  if (!valid) return error(context, request, known_drawable(state.resources(), drawable)
      ? x11::CoreErrorCode::BadMatch : x11::CoreErrorCode::BadDrawable, drawable);
  struct Fill { geometry::Rectangle rectangle; }; std::vector<Fill> fills;
  fills.reserve((request.bytes.size() - 12U) / 8U);
  while (reader.remaining() != 0) { std::uint16_t x{}, y{}, w{}, h{}; (void)reader.read_u16(x); (void)reader.read_u16(y); (void)reader.read_u16(w); (void)reader.read_u16(h); fills.push_back({{static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), w, h}}); }
  auto* storage = mutable_storage(state.resources(), drawable);
  if (!storage) return error(context, request, x11::CoreErrorCode::BadAlloc);
  ClipByChildrenGuard child_clip(state.resources(), drawable, *gc, *storage);
  DispatchResult result;
  geometry::Region damage({0, 0, storage->width(), storage->height()});
  for (const auto& fill : fills) damage.add(fill.rectangle);
  if (supported_window_drawable(state.resources(), drawable))
    result.drawable_damage.reserve(damage.rectangles().size());
  for (const auto& rectangle : damage.rectangles()) {
    if (gc->fill_style == 3)
      fill_opaque_stippled(*storage, rectangle, *gc);
    else
      storage->fill(rectangle, gc->foreground, gc->plane_mask);
  }
  child_clip.restore();
  if (supported_window_drawable(state.resources(), drawable))
    for (const auto& rectangle : damage.rectangles())
      for (const auto visible : child_clip.visible(rectangle)) add_window_damage(result, state.resources(), drawable, visible);
  return result;
}

DispatchResult copy_area_request(ServerState& state, const DispatchContext& context,
                                 const x11::FramedRequest& request) {
  if (!exact_size(request, 28)) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order); std::uint32_t source{}, destination{}, gc_id{};
  std::uint16_t sx{}, sy{}, dx{}, dy{}, width{}, height{};
  (void)reader.read_u32(source); (void)reader.read_u32(destination); (void)reader.read_u32(gc_id);
  (void)reader.read_u16(sx); (void)reader.read_u16(sy); (void)reader.read_u16(dx); (void)reader.read_u16(dy); (void)reader.read_u16(width); (void)reader.read_u16(height);
  auto* gc = state.resources().find_gc(gc_id);
  if (!gc) return error(context, request, x11::CoreErrorCode::BadGContext, gc_id);
  const bool valid_source = state.resources().find_pixmap(source) || supported_window_drawable(state.resources(), source);
  const bool valid_destination = state.resources().find_pixmap(destination) || supported_window_drawable(state.resources(), destination);
  if (!valid_source || !valid_destination) {
    const auto bad = !valid_source ? source : destination;
    return error(context, request, known_drawable(state.resources(), bad)
        ? x11::CoreErrorCode::BadMatch : x11::CoreErrorCode::BadDrawable, bad);
  }
  auto* source_storage = mutable_storage(state.resources(), source); auto* destination_storage = mutable_storage(state.resources(), destination);
  if (!source_storage || !destination_storage) return error(context, request, x11::CoreErrorCode::BadAlloc);
  ClipByChildrenGuard child_clip(state.resources(), destination, *gc, *destination_storage);
  RasterResult raster;
  try { raster = copy_area(*source_storage, *destination_storage, static_cast<std::int16_t>(sx), static_cast<std::int16_t>(sy), width, height, static_cast<std::int16_t>(dx), static_cast<std::int16_t>(dy), gc->plane_mask); }
  catch (const std::bad_alloc&) { return error(context, request, x11::CoreErrorCode::BadAlloc); }
  child_clip.restore(); DispatchResult result;
  if (!raster.damage.empty() && supported_window_drawable(state.resources(), destination))
    for (const auto rectangle : child_clip.visible(raster.damage)) add_window_damage(result, state.resources(), destination, rectangle);
  if (gc->graphics_exposures) {
    const auto requested = geometry::intersect({static_cast<std::int16_t>(dx), static_cast<std::int16_t>(dy), width, height}, {0, 0, destination_storage->width(), destination_storage->height()});
    if (requested && raster.damage == *requested)
      result.output = x11::encode_no_expose(context.byte_order, context.sequence, {destination, 0, request.opcode});
    else if (requested) {
      auto missing = raster.damage.empty() ? std::vector<geometry::Rectangle>{*requested}
                                           : rectangle_difference(*requested, raster.damage);
      result.output.reserve(missing.size() * 32U);
      for (std::size_t index = 0; index < missing.size(); ++index) {
        const auto& rectangle = missing[index];
        auto event = x11::encode_graphics_expose(context.byte_order, context.sequence,
            {destination, static_cast<std::uint16_t>(rectangle.x), static_cast<std::uint16_t>(rectangle.y),
             static_cast<std::uint16_t>(rectangle.width), static_cast<std::uint16_t>(rectangle.height), 0,
             static_cast<std::uint16_t>(missing.size()-index-1), request.opcode});
        result.output.insert(result.output.end(), event.begin(), event.end());
      }
    }
  }
  return result;
}

DispatchResult clear_area(ServerState& state, const DispatchContext& context,
                          const x11::FramedRequest& request) {
  if (!exact_size(request, 16)) return error(context, request, x11::CoreErrorCode::BadLength);
  if (request.data > 1) return error(context, request, x11::CoreErrorCode::BadValue, request.data);
  x11::ByteReader reader(request.body(), context.byte_order); std::uint32_t window_id{}; std::uint16_t x{}, y{}, width{}, height{};
  (void)reader.read_u32(window_id); (void)reader.read_u16(x); (void)reader.read_u16(y); (void)reader.read_u16(width); (void)reader.read_u16(height);
  auto* window = state.resources().find_window(window_id);
  if (!window) return error(context, request, x11::CoreErrorCode::BadWindow, window_id);
  if (!supported_window_drawable(state.resources(), window_id)) return error(context, request, x11::CoreErrorCode::BadMatch, window_id);
  auto* storage = mutable_storage(state.resources(), window_id); if (!storage) return error(context, request, x11::CoreErrorCode::BadAlloc);
  const GraphicsContextResource clear_gc{};
  ClipByChildrenGuard child_clip(state.resources(), window_id, clear_gc, *storage);
  const auto signed_x = static_cast<std::int16_t>(x), signed_y = static_cast<std::int16_t>(y);
  const auto effective_width = width ? width : (signed_x < static_cast<std::int32_t>(window->width) ? window->width - signed_x : 0U);
  const auto effective_height = height ? height : (signed_y < static_cast<std::int32_t>(window->height) ? window->height - signed_y : 0U);
  const auto clipped = geometry::intersect({signed_x, signed_y, effective_width, effective_height}, {0, 0, window->width, window->height});
  DispatchResult result; if (!clipped) return result;
  if (window->attributes.background_source != BackgroundSource::None) {
    storage->fill(*clipped, window->attributes.background_source == BackgroundSource::Pixel ? window->attributes.background_pixel : 0);
    child_clip.restore();
    for (const auto rectangle : child_clip.visible(*clipped))
      add_window_damage(result, state.resources(), window_id, rectangle);
  }
  if (request.data == 1) result.expose_intents.push_back({window_id, *clipped});
  return result;
}


}  // namespace glasswyrm::server::request_handlers
