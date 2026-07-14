#include "glasswyrmd/request_handlers/common.hpp"

#include "glasswyrmd/request_handlers/drawable_access.hpp"
#include "glasswyrmd/font.hpp"
#include "glasswyrmd/m9_raster_ops.hpp"
#include "core/geometry/region.hpp"
#include "protocol/x11/byte_cursor.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace glasswyrm::server::request_handlers {
namespace x11 = gw::protocol::x11;

DispatchResult image_text8(ServerState& state, const DispatchContext& context,
                           const x11::FramedRequest& request) {
  const auto padded = (static_cast<std::size_t>(request.data) + 3U) & ~std::size_t{3U};
  if (!exact_size(request, 16U + padded))
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t drawable{}, gc_id{}; std::uint16_t raw_x{}, raw_y{};
  (void)reader.read_u32(drawable); (void)reader.read_u32(gc_id);
  (void)reader.read_u16(raw_x); (void)reader.read_u16(raw_y);
  const auto* gc = state.resources().find_gc(gc_id);
  if (!gc) return error(context, request, x11::CoreErrorCode::BadGContext, gc_id);
  if (!state.resources().find_pixmap(drawable) && !supported_window_drawable(state.resources(), drawable))
    return error(context, request, known_drawable(state.resources(), drawable)
        ? x11::CoreErrorCode::BadMatch : x11::CoreErrorCode::BadDrawable, drawable);
  auto* storage = mutable_storage(state.resources(), drawable);
  if (!storage) return error(context, request, x11::CoreErrorCode::BadAlloc);
  ClipByChildrenGuard child_clip(state.resources(), drawable, *gc, *storage);
  const auto text = std::span<const std::uint8_t>(request.bytes).subspan(16, request.data);
  const auto raster = raster_text8(*storage, static_cast<std::int16_t>(raw_x),
      static_cast<std::int16_t>(raw_y), text, gc->foreground, gc->background,
      gc->plane_mask, true);
  child_clip.restore(); DispatchResult result;
  if (!raster.damage.empty() && supported_window_drawable(state.resources(), drawable))
    for (const auto rectangle : child_clip.visible(raster.damage)) add_window_damage(result, state.resources(), drawable, rectangle);
  return result;
}

DispatchResult poly_text8(ServerState& state, const DispatchContext& context,
                          const x11::FramedRequest& request) {
  if (request.bytes.size() < 16 || (request.bytes.size() & 3U) != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t drawable{}, gc_id{}; std::uint16_t raw_x{}, raw_y{};
  (void)reader.read_u32(drawable); (void)reader.read_u32(gc_id);
  (void)reader.read_u16(raw_x); (void)reader.read_u16(raw_y);
  const auto* gc = state.resources().find_gc(gc_id);
  if (!gc) return error(context, request, x11::CoreErrorCode::BadGContext, gc_id);
  if (!state.resources().find_pixmap(drawable) && !supported_window_drawable(state.resources(), drawable))
    return error(context, request, known_drawable(state.resources(), drawable)
        ? x11::CoreErrorCode::BadMatch : x11::CoreErrorCode::BadDrawable, drawable);
  struct TextItem { std::int8_t delta{}; std::vector<std::uint8_t> text; };
  std::vector<TextItem> items;
  while (reader.remaining() != 0) {
    std::uint8_t length{}; (void)reader.read_u8(length);
    if (length == 0) continue;
    if (length == 255) {
      std::uint32_t font{}; if (!reader.read_u32(font)) return error(context, request, x11::CoreErrorCode::BadLength);
      if (!state.resources().find_font(font)) return error(context, request, x11::CoreErrorCode::BadFont, font);
      continue;
    }
    std::uint8_t delta{}; if (!reader.read_u8(delta) || reader.remaining() < length)
      return error(context, request, x11::CoreErrorCode::BadLength);
    TextItem item{static_cast<std::int8_t>(delta), {}}; item.text.reserve(length);
    for (std::uint8_t index = 0; index < length; ++index) {
      std::uint8_t value{}; (void)reader.read_u8(value); item.text.push_back(value);
    }
    items.push_back(std::move(item));
  }
  auto* storage = mutable_storage(state.resources(), drawable);
  if (!storage) return error(context, request, x11::CoreErrorCode::BadAlloc);
  ClipByChildrenGuard child_clip(state.resources(), drawable, *gc, *storage);
  std::int32_t x = static_cast<std::int16_t>(raw_x);
  const auto y = static_cast<std::int16_t>(raw_y);
  geometry::Region damage({0, 0, storage->width(), storage->height()});
  for (const auto& item : items) {
    x += item.delta;
    const auto raster = raster_text8(*storage, x, y, item.text, gc->foreground,
                                    gc->background, gc->plane_mask, false);
    if (!raster.damage.empty()) damage.add(raster.damage);
    x += static_cast<std::int32_t>(item.text.size()) * kFixedFontAdvance;
  }
  child_clip.restore(); DispatchResult result;
  if (supported_window_drawable(state.resources(), drawable))
    for (const auto& rectangle : damage.rectangles())
      for (const auto visible : child_clip.visible(rectangle)) add_window_damage(result, state.resources(), drawable, visible);
  return result;
}


}  // namespace glasswyrm::server::request_handlers
