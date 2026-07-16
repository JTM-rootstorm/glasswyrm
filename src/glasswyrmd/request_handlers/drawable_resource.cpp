#include "glasswyrmd/request_handlers/common.hpp"

#include "protocol/x11/byte_cursor.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace glasswyrm::server::request_handlers {
namespace x11 = gw::protocol::x11;

struct GcDecodeResult { bool success{}; x11::CoreErrorCode error{x11::CoreErrorCode::BadImplementation}; std::uint32_t bad{}; GraphicsContextResource gc; };
GcDecodeResult decode_gc_values(x11::ByteReader& reader, std::uint32_t mask,
                                GraphicsContextResource gc,
                                const ResourceTable& resources) {
  constexpr std::uint32_t supported = (1U << 0U) | (1U << 1U) | (1U << 2U) |
      (1U << 3U) | (1U << 4U) | (1U << 5U) | (1U << 6U) |
      (1U << 7U) | (1U << 8U) | (1U << 11U) | (1U << 14U) | (1U << 15U) |
      (1U << 16U) | (1U << 17U) |
      (1U << 18U) | (1U << 19U);
  GcDecodeResult result{}; result.gc = gc;
  if ((mask & ~supported) != 0) return result;
  for (std::uint32_t bit = 0; bit < 23; ++bit) {
    if ((mask & (1U << bit)) == 0) continue;
    std::uint32_t value{}; if (!reader.read_u32(value)) return result; result.bad = value;
    switch (bit) {
      case 0: if (value != 3) { result.error=x11::CoreErrorCode::BadValue; return result; } result.gc.function=3; break;
      case 1: result.gc.plane_mask=value; break;
      case 2: result.gc.foreground=value & 0x00ffffffU; break;
      case 3: result.gc.background=value & 0x00ffffffU; break;
      case 4:
        if (value > std::numeric_limits<std::uint16_t>::max()) {
          result.error=x11::CoreErrorCode::BadValue; return result;
        }
        if (value != 0) return result;
        result.gc.line_width=0; break;
      case 5:
        if (value > 2) { result.error=x11::CoreErrorCode::BadValue; return result; }
        if (value != 0) return result;
        result.gc.line_style=0; break;
      case 6:
        if (value > 3) { result.error=x11::CoreErrorCode::BadValue; return result; }
        if (value != 1) return result;
        result.gc.cap_style=1; break;
      case 7:
        if (value > 2) { result.error=x11::CoreErrorCode::BadValue; return result; }
        if (value != 0) return result;
        result.gc.join_style=0; break;
      case 8:
        if (value > 3) { result.error=x11::CoreErrorCode::BadValue; return result; }
        if (value != 0 && value != 3) return result;
        result.gc.fill_style=static_cast<std::uint8_t>(value); break;
      case 11: {
        const auto* pixmap = resources.find_pixmap(value);
        if (!pixmap) { result.error=x11::CoreErrorCode::BadPixmap; return result; }
        const auto* bitmap = std::get_if<std::shared_ptr<BitmapStorage>>(&pixmap->storage);
        if (pixmap->depth != 1 || pixmap->root != resources.screen().root_window || !bitmap) {
          result.error=x11::CoreErrorCode::BadMatch; return result;
        }
        result.gc.stipple=*bitmap; break;
      }
      case 14:
        if (!resources.find_font(value)) {
          result.error = x11::CoreErrorCode::BadFont;
          return result;
        }
        result.gc.font = kDefaultFontXid;
        break;
      case 15: if (value != 0) { result.error=x11::CoreErrorCode::BadValue; return result; } result.gc.subwindow_mode=0; break;
      case 16: if (value > 1) { result.error=x11::CoreErrorCode::BadValue; return result; } result.gc.graphics_exposures=value != 0; break;
      case 17: result.gc.clip_x_origin=static_cast<std::int16_t>(value); break;
      case 18: result.gc.clip_y_origin=static_cast<std::int16_t>(value); break;
      case 19: if (value != 0) { result.error=x11::CoreErrorCode::BadPixmap; return result; } result.gc.clip_mask=0; break;
      default: break;
    }
  }
  result.success = true; return result;
}

DispatchResult create_pixmap(ServerState& state, const DispatchContext& context,
                             const x11::FramedRequest& request) {
  if (!exact_size(request, 16)) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{}, drawable{}; std::uint16_t width{}, height{};
  (void)reader.read_u32(xid); (void)reader.read_u32(drawable);
  (void)reader.read_u16(width); (void)reader.read_u16(height);
  switch (state.resources().create_pixmap(context.client_id, context.resource_base,
      context.resource_mask, xid, drawable, request.data, width, height)) {
    case CreatePixmapStatus::Success: return {};
    case CreatePixmapStatus::BadIdChoice: return error(context, request, x11::CoreErrorCode::BadIDChoice, xid);
    case CreatePixmapStatus::BadDrawable: return error(context, request, x11::CoreErrorCode::BadDrawable, drawable);
    case CreatePixmapStatus::BadValue: return error(context, request, x11::CoreErrorCode::BadValue, request.data == 24 && width && height ? 0 : request.data);
    case CreatePixmapStatus::BadMatch: return error(context, request, x11::CoreErrorCode::BadMatch, drawable);
    case CreatePixmapStatus::BadAlloc: return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return {};
}

DispatchResult free_pixmap(ServerState& state, const DispatchContext& context,
                           const x11::FramedRequest& request) {
  if (!exact_size(request, 8)) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order); std::uint32_t xid{}; (void)reader.read_u32(xid);
  return state.resources().free_pixmap(xid) == FreePixmapStatus::Success
      ? DispatchResult{} : error(context, request, x11::CoreErrorCode::BadPixmap, xid);
}

DispatchResult create_gc(ServerState& state, const DispatchContext& context,
                         const x11::FramedRequest& request) {
  if (request.bytes.size() < 16) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order); std::uint32_t xid{}, drawable{}, mask{};
  (void)reader.read_u32(xid); (void)reader.read_u32(drawable); (void)reader.read_u32(mask);
  if (!exact_size(request, 16 + static_cast<std::size_t>(std::popcount(mask)) * 4U))
    return error(context, request, x11::CoreErrorCode::BadLength);
  const auto decoded = decode_gc_values(reader, mask, {}, state.resources());
  if (!decoded.success) return error(context, request, decoded.error, decoded.bad);
  switch (state.resources().create_gc(context.client_id, context.resource_base,
      context.resource_mask, xid, drawable, decoded.gc)) {
    case CreateGcStatus::Success: return {};
    case CreateGcStatus::BadIdChoice: return error(context, request, x11::CoreErrorCode::BadIDChoice, xid);
    case CreateGcStatus::BadDrawable: return error(context, request, x11::CoreErrorCode::BadDrawable, drawable);
    case CreateGcStatus::BadMatch: return error(context, request, x11::CoreErrorCode::BadMatch, drawable);
    case CreateGcStatus::BadAlloc: return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return {};
}

DispatchResult change_gc(ServerState& state, const DispatchContext& context,
                         const x11::FramedRequest& request) {
  if (request.bytes.size() < 12) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order); std::uint32_t xid{}, mask{};
  (void)reader.read_u32(xid); (void)reader.read_u32(mask);
  if (!exact_size(request, 12 + static_cast<std::size_t>(std::popcount(mask)) * 4U))
    return error(context, request, x11::CoreErrorCode::BadLength);
  auto* gc = state.resources().find_gc(xid);
  if (!gc) return error(context, request, x11::CoreErrorCode::BadGContext, xid);
  const auto decoded = decode_gc_values(reader, mask, *gc, state.resources());
  if (!decoded.success) return error(context, request, decoded.error, decoded.bad);
  *gc = decoded.gc; return {};
}

DispatchResult free_gc(ServerState& state, const DispatchContext& context,
                       const x11::FramedRequest& request) {
  if (!exact_size(request, 8)) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order); std::uint32_t xid{}; (void)reader.read_u32(xid);
  return state.resources().free_gc(xid) == FreeGcStatus::Success
      ? DispatchResult{} : error(context, request, x11::CoreErrorCode::BadGContext, xid);
}


}  // namespace glasswyrm::server::request_handlers
