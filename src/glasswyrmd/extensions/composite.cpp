#include "glasswyrmd/extensions/composite.hpp"

#include "glasswyrmd/request_handlers/common.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/reply.hpp"

#include <algorithm>

namespace glasswyrm::server::extensions {
namespace x11 = gw::protocol::x11;
using request_handlers::error;

namespace {

DispatchResult query_version(const DispatchContext& context,
                             const x11::FramedRequest& request) {
  if (request.core_size() != 12)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t major{}, minor{};
  (void)reader.read_u32(major);
  (void)reader.read_u32(minor);
  if (major != 0) {
    major = 0;
    minor = 4;
  } else {
    minor = std::min(minor, std::uint32_t{4});
  }
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u32(major);
  reply.write_u32(minor);
  reply.write_padding(16);
  return {std::move(reply).finish()};
}

DispatchResult redirect(ServerState& state, const DispatchContext& context,
                        const x11::FramedRequest& request,
                        const CompositeRedirectScope scope,
                        const bool enable) {
  if (request.core_size() != 12)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t window{};
  std::uint8_t raw_mode{};
  (void)reader.read_u32(window);
  (void)reader.read_u8(raw_mode);
  if (!state.resources().find_window(window))
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  if (scope == CompositeRedirectScope::Direct &&
      window == state.screen().root_window)
    return error(context, request, x11::CoreErrorCode::BadMatch, window);
  if (raw_mode > 1)
    return error(context, request, x11::CoreErrorCode::BadValue, raw_mode);
  const auto mode = static_cast<CompositeRedirectMode>(raw_mode);
  const auto status =
      enable ? state.composite().redirect(context.client_id, window, scope, mode)
             : state.composite().unredirect(context.client_id, window, scope,
                                            mode);
  switch (status) {
    case CompositeRedirectStatus::Success: return {};
    case CompositeRedirectStatus::ManualConflict:
      return error(context, request, x11::CoreErrorCode::BadAccess, window);
    case CompositeRedirectStatus::BadAlloc:
      return error(context, request, x11::CoreErrorCode::BadAlloc);
    case CompositeRedirectStatus::NotRedirected:
    case CompositeRedirectStatus::NotOwner:
    case CompositeRedirectStatus::InvalidMode:
    case CompositeRedirectStatus::InvalidScope:
      return error(context, request, x11::CoreErrorCode::BadValue, raw_mode);
    case CompositeRedirectStatus::InvalidWindow:
      return error(context, request, x11::CoreErrorCode::BadWindow, window);
  }
  return error(context, request, x11::CoreErrorCode::BadImplementation);
}

bool effectively_redirected(const ServerState& state, std::uint32_t window) {
  if (state.composite().redirected(window, CompositeRedirectScope::Direct))
    return true;
  while (window != state.screen().root_window) {
    const auto* resource = state.resources().find_window(window);
    if (!resource) return false;
    window = resource->parent;
    if (state.composite().redirected(window, CompositeRedirectScope::Subtree))
      return true;
  }
  return false;
}

DispatchResult name_window_pixmap(ServerState& state,
                                  const DispatchContext& context,
                                  const x11::FramedRequest& request) {
  if (request.core_size() != 12)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t window{}, pixmap{};
  (void)reader.read_u32(window);
  (void)reader.read_u32(pixmap);
  if (!state.resources().find_window(window))
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  if (window == state.screen().root_window || !effectively_redirected(state, window))
    return error(context, request, x11::CoreErrorCode::BadMatch, window);
  switch (state.resources().name_window_pixmap(
      context.client_id, context.resource_base, context.resource_mask, pixmap,
      window)) {
    case CreatePixmapStatus::Success: return {};
    case CreatePixmapStatus::BadIdChoice:
      return error(context, request, x11::CoreErrorCode::BadIDChoice, pixmap);
    case CreatePixmapStatus::BadDrawable:
      return error(context, request, x11::CoreErrorCode::BadWindow, window);
    case CreatePixmapStatus::BadValue:
      return error(context, request, x11::CoreErrorCode::BadValue);
    case CreatePixmapStatus::BadMatch:
      return error(context, request, x11::CoreErrorCode::BadMatch, window);
    case CreatePixmapStatus::BadAlloc:
      return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return error(context, request, x11::CoreErrorCode::BadImplementation);
}

}  // namespace

DispatchResult dispatch_composite(ServerState& state,
                                  const DispatchContext& context,
                                  const x11::FramedRequest& request) {
  switch (request.data) {
    case 0: return query_version(context, request);
    case 1:
      return redirect(state, context, request, CompositeRedirectScope::Direct,
                      true);
    case 2:
      return redirect(state, context, request, CompositeRedirectScope::Subtree,
                      true);
    case 3:
      return redirect(state, context, request, CompositeRedirectScope::Direct,
                      false);
    case 4:
      return redirect(state, context, request, CompositeRedirectScope::Subtree,
                      false);
    case 6: return name_window_pixmap(state, context, request);
    default: return error(context, request, x11::CoreErrorCode::BadRequest);
  }
}

}  // namespace glasswyrm::server::extensions
