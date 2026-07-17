#include "glasswyrmd/extensions/damage.hpp"

#include "glasswyrmd/extension_event_helpers.hpp"
#include "glasswyrmd/extension_registry.hpp"
#include "glasswyrmd/extension_wire.hpp"
#include "glasswyrmd/request_handlers/common.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/reply.hpp"

#include <optional>

namespace glasswyrm::server::extensions {
namespace x11 = gw::protocol::x11;
using request_handlers::error;

DispatchResult extension_resource_error(const ExtensionKind kind,
                                        const DispatchContext& context,
                                        const x11::FramedRequest& request,
                                        const std::uint32_t xid) {
  const auto* extension = find_extension(kind);
  const auto packet = extension
                          ? encode_extension_error(
                                context.byte_order, *extension, 0,
                                context.sequence, xid, request.opcode,
                                request.data)
                          : std::nullopt;
  return packet ? DispatchResult{*packet}
                : error(context, request,
                        x11::CoreErrorCode::BadImplementation);
}

DispatchResult damage_status(const DamageStatus status,
                             const DispatchContext& context,
                             const x11::FramedRequest& request,
                             const std::uint32_t xid) {
  switch (status) {
    case DamageStatus::Success: return {};
    case DamageStatus::BadIdChoice:
      return error(context, request, x11::CoreErrorCode::BadIDChoice, xid);
    case DamageStatus::BadDamage:
      return extension_resource_error(ExtensionKind::Damage, context, request,
                                      xid);
    case DamageStatus::BadDrawable:
      return error(context, request, x11::CoreErrorCode::BadDrawable, xid);
    case DamageStatus::BadValue:
      return error(context, request, x11::CoreErrorCode::BadValue, xid);
    case DamageStatus::BadAlloc:
      return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return error(context, request, x11::CoreErrorCode::BadImplementation);
}

DispatchResult damage_query_version(const DispatchContext& context,
                                    const x11::FramedRequest& request) {
  if (request.core_size() != 12)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t major{}, minor{};
  (void)reader.read_u32(major);
  (void)reader.read_u32(minor);
  if (major > 1) {
    major = 1;
    minor = 1;
  } else if (major == 1) {
    minor = std::min(minor, std::uint32_t{1});
  }
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u32(major);
  reply.write_u32(minor);
  reply.write_padding(16);
  return {std::move(reply).finish()};
}

DispatchResult damage_create(ServerState& state, const DispatchContext& context,
                             const x11::FramedRequest& request) {
  if (request.core_size() != 16)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{}, drawable{};
  std::uint8_t raw_level{};
  (void)reader.read_u32(xid);
  (void)reader.read_u32(drawable);
  (void)reader.read_u8(raw_level);
  if (raw_level < 2 || raw_level > 3)
    return error(context, request, x11::CoreErrorCode::BadValue, raw_level);
  return damage_status(
      state.resources().create_damage(
          context.client_id, context.resource_base, context.resource_mask, xid,
          drawable, static_cast<DamageReportLevel>(raw_level)),
      context, request,
      state.resources().find_window(drawable) ||
              state.resources().find_pixmap(drawable)
          ? xid
          : drawable);
}

DispatchResult damage_destroy(ServerState& state,
                              const DispatchContext& context,
                              const x11::FramedRequest& request) {
  if (request.core_size() != 8)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{};
  (void)reader.read_u32(xid);
  return damage_status(state.resources().destroy_damage(xid), context, request,
                       xid);
}

DispatchResult damage_subtract(ServerState& state,
                               const DispatchContext& context,
                               const x11::FramedRequest& request) {
  if (request.core_size() != 16)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t damage{}, repair{}, parts{};
  (void)reader.read_u32(damage);
  (void)reader.read_u32(repair);
  (void)reader.read_u32(parts);
  if (!state.resources().find_damage(damage))
    return extension_resource_error(ExtensionKind::Damage, context, request,
                                    damage);
  for (const auto region : {repair, parts})
    if (region != 0 && !state.resources().find_xfixes_region(region))
      return extension_resource_error(ExtensionKind::XFixes, context, request,
                                      region);
  return damage_status(
      state.resources().subtract_damage(damage, repair, parts), context,
      request, damage);
}

DispatchResult damage_add(ServerState& state, const DispatchContext& context,
                          const x11::FramedRequest& request) {
  if (request.core_size() != 12)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t drawable{}, region{};
  (void)reader.read_u32(drawable);
  (void)reader.read_u32(region);
  if (!state.resources().find_window(drawable) &&
      !state.resources().find_pixmap(drawable))
    return error(context, request, x11::CoreErrorCode::BadDrawable, drawable);
  const auto* region_resource = state.resources().find_xfixes_region(region);
  if (!region_resource)
    return extension_resource_error(ExtensionKind::XFixes, context, request,
                                    region);
  DispatchResult result;
  for (const auto rectangle : region_resource->rectangles)
    append_damage_notifications(
        result, state.resources().damage_drawable(drawable, rectangle),
        context.input.logical_time);
  return result;
}

DispatchResult dispatch_damage(ServerState& state,
                               const DispatchContext& context,
                               const x11::FramedRequest& request) {
  switch (request.data) {
    case 0: return damage_query_version(context, request);
    case 1: return damage_create(state, context, request);
    case 2: return damage_destroy(state, context, request);
    case 3: return damage_subtract(state, context, request);
    case 4: return damage_add(state, context, request);
    default: return error(context, request, x11::CoreErrorCode::BadRequest);
  }
}

}  // namespace glasswyrm::server::extensions
