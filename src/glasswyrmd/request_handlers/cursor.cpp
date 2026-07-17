#include "glasswyrmd/request_handlers/common.hpp"

#include "input/cursor_model.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/reply.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

namespace glasswyrm::server::request_handlers {
namespace x11 = gw::protocol::x11;

namespace {

bool read_color(x11::ByteReader& reader, input::CursorColor& color) {
  return reader.read_u16(color.red) && reader.read_u16(color.green) &&
         reader.read_u16(color.blue);
}

DispatchResult store_cursor(ServerState& state, const DispatchContext& context,
                            const x11::FramedRequest& request,
                            const std::uint32_t xid,
                            std::shared_ptr<const input::CursorImage> image) {
  switch (state.resources().create_cursor(
      context.client_id, context.resource_base, context.resource_mask, xid,
      std::move(image))) {
    case CreateCursorStatus::Success:
      return {};
    case CreateCursorStatus::BadIdChoice:
      return error(context, request, x11::CoreErrorCode::BadIDChoice, xid);
    case CreateCursorStatus::BadAlloc:
      return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return {};
}

input::CursorFontIdentity cursor_font_identity(
    const FontIdentity identity) noexcept {
  switch (identity) {
    case FontIdentity::Cursor: return input::CursorFontIdentity::Cursor;
    case FontIdentity::Fixed: return input::CursorFontIdentity::Fixed;
    case FontIdentity::Nil2: return input::CursorFontIdentity::Nil2;
  }
  return input::CursorFontIdentity::Fixed;
}

}  // namespace

DispatchResult create_cursor(ServerState& state, const DispatchContext& context,
                             const x11::FramedRequest& request) {
  if (!exact_size(request, 32))
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid = 0, source_id = 0, mask_id = 0;
  input::CursorColor foreground, background;
  std::uint16_t hotspot_x = 0, hotspot_y = 0;
  if (!reader.read_u32(xid) || !reader.read_u32(source_id) ||
      !reader.read_u32(mask_id) || !read_color(reader, foreground) ||
      !read_color(reader, background) || !reader.read_u16(hotspot_x) ||
      !reader.read_u16(hotspot_y))
    return error(context, request, x11::CoreErrorCode::BadLength);

  const auto* source = state.resources().find_pixmap(source_id);
  const auto* mask = state.resources().find_pixmap(mask_id);
  if (!source)
    return error(context, request, x11::CoreErrorCode::BadPixmap, source_id);
  if (!mask)
    return error(context, request, x11::CoreErrorCode::BadPixmap, mask_id);
  if (source->depth != 1 || mask->depth != 1 ||
      source->width != mask->width || source->height != mask->height)
    return error(context, request, x11::CoreErrorCode::BadMatch);
  if (source->width > 64 || source->height > 64)
    return error(context, request, x11::CoreErrorCode::BadAlloc);
  if (hotspot_x >= source->width || hotspot_y >= source->height)
    return error(context, request, x11::CoreErrorCode::BadMatch);
  const auto* source_bitmap = source->bitmap();
  const auto* mask_bitmap = mask->bitmap();
  if (!source_bitmap || !mask_bitmap)
    return error(context, request, x11::CoreErrorCode::BadMatch);

  std::string cursor_error;
  auto image = input::make_pixmap_cursor(
      {source_id, mask_id, source->width, source->height, hotspot_x, hotspot_y,
       source_bitmap->bits(), mask_bitmap->bits(), foreground, background},
      cursor_error);
  if (!image) return error(context, request, x11::CoreErrorCode::BadAlloc);
  return store_cursor(state, context, request, xid, std::move(image));
}

DispatchResult create_glyph_cursor(ServerState& state,
                                   const DispatchContext& context,
                                   const x11::FramedRequest& request) {
  if (!exact_size(request, 32))
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid = 0, source_font_id = 0, mask_font_id = 0;
  std::uint16_t source_character = 0, mask_character = 0;
  input::CursorColor foreground, background;
  if (!reader.read_u32(xid) || !reader.read_u32(source_font_id) ||
      !reader.read_u32(mask_font_id) || !reader.read_u16(source_character) ||
      !reader.read_u16(mask_character) || !read_color(reader, foreground) ||
      !read_color(reader, background))
    return error(context, request, x11::CoreErrorCode::BadLength);
  const auto* source_font = state.resources().find_font(source_font_id);
  const auto* mask_font = state.resources().find_font(mask_font_id);
  if (!source_font)
    return error(context, request, x11::CoreErrorCode::BadFont,
                 source_font_id);
  if (!mask_font)
    return error(context, request, x11::CoreErrorCode::BadFont, mask_font_id);

  std::string cursor_error;
  auto image = input::make_glyph_cursor(
      {cursor_font_identity(source_font->identity),
       cursor_font_identity(mask_font->identity), source_character,
       mask_character, foreground, background},
      cursor_error);
  if (!image)
    return error(context, request, x11::CoreErrorCode::BadValue,
                 source_character);
  return store_cursor(state, context, request, xid, std::move(image));
}

DispatchResult free_cursor(ServerState& state, const DispatchContext& context,
                           const x11::FramedRequest& request) {
  if (!exact_size(request, 8))
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid = 0;
  (void)reader.read_u32(xid);
  return state.resources().free_cursor(xid) == FreeCursorStatus::Success
             ? DispatchResult{}
             : error(context, request, x11::CoreErrorCode::BadCursor, xid);
}

DispatchResult recolor_cursor(ServerState& state,
                              const DispatchContext& context,
                              const x11::FramedRequest& request) {
  if (!exact_size(request, 20))
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid = 0;
  input::CursorColor foreground, background;
  if (!reader.read_u32(xid) || !read_color(reader, foreground) ||
      !read_color(reader, background))
    return error(context, request, x11::CoreErrorCode::BadLength);
  switch (state.resources().recolor_cursor(xid, foreground, background)) {
    case RecolorCursorStatus::Success:
      return {};
    case RecolorCursorStatus::BadCursor:
      return error(context, request, x11::CoreErrorCode::BadCursor, xid);
    case RecolorCursorStatus::BadAlloc:
      return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return {};
}

DispatchResult query_best_size(const ServerState& state,
                               const DispatchContext& context,
                               const x11::FramedRequest& request) {
  if (!exact_size(request, 12))
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t drawable = 0;
  std::uint16_t width = 0, height = 0;
  if (!reader.read_u32(drawable) || !reader.read_u16(width) ||
      !reader.read_u16(height))
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (!state.resources().find_window(drawable) &&
      !state.resources().find_pixmap(drawable))
    return error(context, request, x11::CoreErrorCode::BadDrawable, drawable);
  if (request.data > 2)
    return error(context, request, x11::CoreErrorCode::BadValue, request.data);
  if (request.data == 0) {
    width = 64;
    height = 64;
  } else {
    constexpr std::uint16_t kMaximumPatternExtent = 16384;
    width = std::clamp<std::uint16_t>(width, 1, kMaximumPatternExtent);
    height = std::clamp<std::uint16_t>(height, 1, kMaximumPatternExtent);
  }
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u16(width);
  reply.write_u16(height);
  return {std::move(reply).finish()};
}

}  // namespace glasswyrm::server::request_handlers
