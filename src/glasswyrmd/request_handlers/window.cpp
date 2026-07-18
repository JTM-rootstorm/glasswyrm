#include "glasswyrmd/request_handlers/window_attributes.hpp"

#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/event_mask.hpp"
#include "protocol/x11/reply.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace glasswyrm::server::request_handlers {
namespace x11 = gw::protocol::x11;

constexpr std::uint32_t kWindowAttributeMask = 0x00007fffU;
constexpr auto kCoreEventMask = x11::event_mask::All;
constexpr auto kDoNotPropagateMask = x11::event_mask::DoNotPropagate;
constexpr auto kButtonPressMask = x11::event_mask::ButtonPress;
constexpr auto kResizeRedirectMask = x11::event_mask::ResizeRedirect;
constexpr auto kSubstructureRedirectMask = x11::event_mask::SubstructureRedirect;
DecodedWindowAttributes decode_window_attributes(
    x11::ByteReader& reader, const std::uint32_t value_mask,
    WindowAttributes attributes, const ResourceTable& resources) {
  DecodedWindowAttributes result;
  result.attributes = attributes;
  for (std::uint32_t bit = 0; bit < 15; ++bit) {
    if ((value_mask & (std::uint32_t{1} << bit)) == 0) continue;
    std::uint32_t value = 0;
    if (!reader.read_u32(value)) return result;
    result.bad_value = value;
    switch (bit) {
      case 0:
        if (value > 1) { result.error = x11::CoreErrorCode::BadPixmap; return result; }
        result.attributes.background_pixmap = value;
        result.attributes.background_source = value == 0
            ? BackgroundSource::None : BackgroundSource::ParentRelative;
        break;
      case 1:
        result.attributes.background_pixel = value & 0x00ffffffU;
        result.attributes.background_source = BackgroundSource::Pixel;
        break;
      case 2:
        if (value != 0) { result.error = x11::CoreErrorCode::BadPixmap; return result; }
        result.attributes.border_pixmap = value;
        break;
      case 3: result.attributes.border_pixel = value; break;
      case 4:
        if (value > 10) { result.error = x11::CoreErrorCode::BadValue; return result; }
        result.attributes.bit_gravity = static_cast<std::uint8_t>(value);
        break;
      case 5:
        if (value > 10) { result.error = x11::CoreErrorCode::BadValue; return result; }
        result.attributes.window_gravity = static_cast<std::uint8_t>(value);
        break;
      case 6:
        if (value > 2) { result.error = x11::CoreErrorCode::BadValue; return result; }
        result.attributes.backing_store = static_cast<std::uint8_t>(value);
        break;
      case 7: result.attributes.backing_planes = value; break;
      case 8: result.attributes.backing_pixel = value; break;
      case 9:
        if (value > 1) { result.error = x11::CoreErrorCode::BadValue; return result; }
        result.attributes.override_redirect = value != 0;
        break;
      case 10:
        if (value > 1) { result.error = x11::CoreErrorCode::BadValue; return result; }
        result.attributes.save_under = value != 0;
        break;
      case 11:
        if ((value & ~kCoreEventMask) != 0) { result.error = x11::CoreErrorCode::BadValue; return result; }
        result.event_mask = value;
        break;
      case 12:
        if ((value & ~kDoNotPropagateMask) != 0) { result.error = x11::CoreErrorCode::BadValue; return result; }
        result.attributes.do_not_propagate_mask = value;
        break;
      case 13:
        if (value != 0 && !resources.valid_colormap(value)) { result.error = x11::CoreErrorCode::BadColormap; return result; }
        result.attributes.colormap = value;
        break;
      case 14:
        if (value == 0) {
          result.attributes.cursor = 0;
          result.attributes.cursor_inherit = true;
          result.attributes.cursor_image.reset();
          break;
        }
        if (const auto* cursor = resources.find_cursor(value)) {
          result.attributes.cursor = value;
          result.attributes.cursor_inherit = false;
          result.attributes.cursor_image = cursor->image;
          break;
        }
        result.error = x11::CoreErrorCode::BadCursor;
        return result;
      default: break;
    }
  }
  result.success = true;
  return result;
}

DispatchResult change_window_attributes(
    ServerState& state, const DispatchContext& context,
    const x11::FramedRequest& request) {
  if (request.bytes.size() < 12) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t window_id = 0;
  std::uint32_t value_mask = 0;
  if (!reader.read_u32(window_id) || !reader.read_u32(value_mask)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  if ((value_mask & ~kWindowAttributeMask) != 0) {
    return error(context, request, x11::CoreErrorCode::BadValue, value_mask);
  }
  const auto value_count = static_cast<std::size_t>(std::popcount(value_mask));
  if (!exact_size(request, 12 + value_count * 4)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  auto* window = state.resources().find_window(window_id);
  if (window == nullptr) {
    return error(context, request, x11::CoreErrorCode::BadWindow, window_id);
  }
  if (context.integrated_lifecycle &&
      state.resources().cleanup_pending(window_id))
    return error(context, request, x11::CoreErrorCode::BadWindow, window_id);
  auto decoded = decode_window_attributes(reader, value_mask,
                                          window->attributes,
                                          state.resources());
  if (!decoded.success) {
    return error(context, request, decoded.error, decoded.bad_value);
  }
  if ((value_mask & (1U << 9U)) != 0 &&
      window_id == state.screen().root_window) {
    return error(context, request, x11::CoreErrorCode::BadMatch, window_id);
  }
  const bool defer_override =
      context.integrated_lifecycle && (value_mask & (1U << 9U)) != 0 &&
      state.resources().is_policy_candidate(window_id) &&
      window->map_requested &&
      decoded.attributes.override_redirect !=
          window->attributes.override_redirect;
  const bool proposed_override = decoded.attributes.override_redirect;
  if (decoded.event_mask.has_value()) {
    const auto selected = *decoded.event_mask;
    if ((selected & (kResizeRedirectMask | kSubstructureRedirectMask)) != 0) {
      return error(context, request, x11::CoreErrorCode::BadAccess, selected);
    }
    if ((selected & kButtonPressMask) != 0) {
      for (const auto& [client, mask] : window->event_selections) {
        if (client != context.client_id && (mask & kButtonPressMask) != 0) {
          return error(context, request, x11::CoreErrorCode::BadAccess,
                       selected);
        }
      }
    }
    // Updating the selection first preserves atomicity if insertion allocates.
    if (!state.resources().set_event_selection(window_id, context.client_id,
                                               selected)) {
      return error(context, request, x11::CoreErrorCode::BadWindow, window_id);
    }
  }
  if (defer_override)
    decoded.attributes.override_redirect = window->attributes.override_redirect;
  window->attributes = decoded.attributes;
  if (defer_override)
    return DispatchResult::deferred_override_change(window_id,
                                                    proposed_override);
  return {};
}

DispatchResult get_window_attributes(ServerState& state,
                                     const DispatchContext& context,
                                     const x11::FramedRequest& request) {
  if (!exact_size(request, 8)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t window_id = 0;
  (void)reader.read_u32(window_id);
  const auto* window = state.resources().find_window(window_id);
  if (window == nullptr) {
    return error(context, request, x11::CoreErrorCode::BadWindow, window_id);
  }
  x11::ReplyBuilder reply(context.byte_order, context.sequence,
                          window->attributes.backing_store);
  reply.write_u32(window->visual);
  reply.write_u16(static_cast<std::uint16_t>(window->window_class));
  reply.write_u8(window->attributes.bit_gravity);
  reply.write_u8(window->attributes.window_gravity);
  reply.write_u32(window->attributes.backing_planes);
  reply.write_u32(window->attributes.backing_pixel);
  reply.write_u8(window->attributes.save_under ? 1 : 0);
  reply.write_u8(window->attributes.colormap == state.screen().default_colormap
                     ? 1
                     : 0);
  reply.write_u8(static_cast<std::uint8_t>(window->map_state));
  reply.write_u8(window->attributes.override_redirect ? 1 : 0);
  reply.write_u32(window->attributes.colormap);
  reply.write_payload_u32(state.resources().all_event_selections(window_id));
  reply.write_payload_u32(
      state.resources().event_selection(window_id, context.client_id));
  reply.write_payload_u16(static_cast<std::uint16_t>(
      window->attributes.do_not_propagate_mask));
  reply.write_payload_u16(0);
  return {std::move(reply).finish()};
}

DispatchResult get_geometry(ServerState& state,
                            const DispatchContext& context,
                            const x11::FramedRequest& request) {
  if (!exact_size(request, 8)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t drawable = 0;
  (void)reader.read_u32(drawable);
  const auto* window = state.resources().find_window(drawable);
  auto* pixmap = state.resources().find_pixmap(drawable);
  if (window == nullptr && pixmap == nullptr) {
    return error(context, request, x11::CoreErrorCode::BadDrawable, drawable);
  }
  x11::ReplyBuilder reply(context.byte_order, context.sequence,
                          window ? window->depth : pixmap->depth);
  reply.write_u32(state.screen().root_window);
  reply.write_u16(window ? static_cast<std::uint16_t>(window->x) : 0);
  reply.write_u16(window ? static_cast<std::uint16_t>(window->y) : 0);
  reply.write_u16(window ? window->width : pixmap->width);
  reply.write_u16(window ? window->height : pixmap->height);
  reply.write_u16(window ? window->border_width : 0);
  reply.write_padding(2);
  return {std::move(reply).finish()};
}


}  // namespace glasswyrm::server::request_handlers
