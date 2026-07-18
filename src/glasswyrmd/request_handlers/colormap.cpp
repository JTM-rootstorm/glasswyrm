#include "glasswyrmd/request_handlers/common.hpp"

#include "glasswyrmd/extension_registry.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/reply.hpp"

namespace glasswyrm::server::request_handlers {
namespace x11 = gw::protocol::x11;

bool colormap_profile_enabled(const DispatchContext& context) noexcept {
  return context.extensions && context.extensions->profile_enabled(
                                   ExtensionCapability::GameCompat);
}

DispatchResult create_colormap(ServerState& state,
                               const DispatchContext& context,
                               const x11::FramedRequest& request) {
  if (!colormap_profile_enabled(context))
    return error(context, request, x11::CoreErrorCode::BadRequest);
  if (!exact_size(request, 16))
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (request.data != 0)
    return error(context, request, x11::CoreErrorCode::BadValue, request.data);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{}, window{}, visual{};
  if (!reader.read_u32(xid) || !reader.read_u32(window) ||
      !reader.read_u32(visual))
    return error(context, request, x11::CoreErrorCode::BadLength);
  switch (state.resources().create_colormap(
      context.client_id, context.resource_base, context.resource_mask, xid,
      window, visual)) {
    case CreateColormapStatus::Success: return {};
    case CreateColormapStatus::BadIdChoice:
      return error(context, request, x11::CoreErrorCode::BadIDChoice, xid);
    case CreateColormapStatus::BadWindow:
      return error(context, request, x11::CoreErrorCode::BadWindow, window);
    case CreateColormapStatus::BadMatch:
      return error(context, request, x11::CoreErrorCode::BadMatch, visual);
    case CreateColormapStatus::BadAlloc:
      return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return error(context, request, x11::CoreErrorCode::BadImplementation);
}

DispatchResult free_colormap(ServerState& state,
                             const DispatchContext& context,
                             const x11::FramedRequest& request) {
  if (!colormap_profile_enabled(context))
    return error(context, request, x11::CoreErrorCode::BadRequest);
  if (!exact_size(request, 8))
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{};
  (void)reader.read_u32(xid);
  switch (state.resources().free_colormap(xid)) {
    case FreeColormapStatus::Success: return {};
    case FreeColormapStatus::BadColormap:
      return error(context, request, x11::CoreErrorCode::BadColormap, xid);
    case FreeColormapStatus::BadAccess:
      return error(context, request, x11::CoreErrorCode::BadAccess, xid);
  }
  return error(context, request, x11::CoreErrorCode::BadImplementation);
}

DispatchResult install_colormap(const ServerState& state,
                                const DispatchContext& context,
                                const x11::FramedRequest& request) {
  if (!colormap_profile_enabled(context))
    return error(context, request, x11::CoreErrorCode::BadRequest);
  if (!exact_size(request, 8))
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{};
  (void)reader.read_u32(xid);
  if (!state.resources().valid_colormap(xid))
    return error(context, request, x11::CoreErrorCode::BadColormap, xid);
  return {};
}

DispatchResult list_installed_colormaps(
    const ServerState& state, const DispatchContext& context,
    const x11::FramedRequest& request) {
  if (!colormap_profile_enabled(context))
    return error(context, request, x11::CoreErrorCode::BadRequest);
  if (!exact_size(request, 8))
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t window{};
  (void)reader.read_u32(window);
  if (!state.resources().find_window(window))
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u16(1);
  reply.write_padding(22);
  reply.write_payload_u32(state.screen().default_colormap);
  return {std::move(reply).finish()};
}

}  // namespace glasswyrm::server::request_handlers
