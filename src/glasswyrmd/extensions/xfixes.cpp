#include "glasswyrmd/extensions/xfixes.hpp"

#include "glasswyrmd/extension_registry.hpp"
#include "glasswyrmd/extension_wire.hpp"
#include "glasswyrmd/request_handlers/common.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/reply.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace glasswyrm::server::extensions {
namespace x11 = gw::protocol::x11;
using request_handlers::error;

constexpr std::uint8_t kQueryVersion = 0;
constexpr std::uint8_t kSelectSelectionInput = 2;
constexpr std::uint8_t kCreateRegion = 5;
constexpr std::uint8_t kDestroyRegion = 10;
constexpr std::uint8_t kSetRegion = 11;
constexpr std::uint8_t kCopyRegion = 12;
constexpr std::uint8_t kUnionRegion = 13;
constexpr std::uint8_t kIntersectRegion = 14;
constexpr std::uint8_t kSubtractRegion = 15;
constexpr std::uint8_t kTranslateRegion = 17;
constexpr std::uint8_t kRegionExtents = 18;
constexpr std::uint8_t kFetchRegion = 19;

DispatchResult bad_region(const DispatchContext& context,
                          const x11::FramedRequest& request,
                          const std::uint32_t xid) {
  const auto* extension = find_extension(ExtensionKind::XFixes);
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

DispatchResult region_status(const RegionStatus status,
                             const DispatchContext& context,
                             const x11::FramedRequest& request,
                             const std::uint32_t xid) {
  switch (status) {
    case RegionStatus::Success: return {};
    case RegionStatus::BadIdChoice:
      return error(context, request, x11::CoreErrorCode::BadIDChoice, xid);
    case RegionStatus::BadRegion: return bad_region(context, request, xid);
    case RegionStatus::BadValue:
      return error(context, request, x11::CoreErrorCode::BadValue, xid);
    case RegionStatus::BadAlloc:
      return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return error(context, request, x11::CoreErrorCode::BadImplementation);
}

std::optional<std::vector<geometry::Rectangle>> read_rectangles(
    const x11::FramedRequest& request, const x11::ByteOrder order) {
  if (request.core_size() < 8 || (request.core_size() - 8U) % 8U != 0)
    return std::nullopt;
  const auto count = (request.core_size() - 8U) / 8U;
  if (count > kMaximumXFixesRegionRectangles) return std::nullopt;
  x11::ByteReader reader(request.body().subspan(4), order);
  std::vector<geometry::Rectangle> result;
  result.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    std::uint16_t x{}, y{}, width{}, height{};
    if (!reader.read_u16(x) || !reader.read_u16(y) ||
        !reader.read_u16(width) || !reader.read_u16(height))
      return std::nullopt;
    result.push_back({static_cast<std::int16_t>(x),
                      static_cast<std::int16_t>(y), width, height});
  }
  return result;
}

DispatchResult xfixes_query_version(const DispatchContext& context,
                                    const x11::FramedRequest& request) {
  if (request.core_size() != 12)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t major{}, minor{};
  (void)reader.read_u32(major);
  (void)reader.read_u32(minor);
  if (major > 2) {
    major = 2;
    minor = 0;
  } else if (major == 2) {
    minor = 0;
  }
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u32(major);
  reply.write_u32(minor);
  reply.write_padding(16);
  return {std::move(reply).finish()};
}

DispatchResult select_selection(ServerState& state,
                                const DispatchContext& context,
                                const x11::FramedRequest& request) {
  if (request.core_size() != 16)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t window{}, selection{}, mask{};
  (void)reader.read_u32(window);
  (void)reader.read_u32(selection);
  (void)reader.read_u32(mask);
  if (!state.resources().find_window(window))
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  const auto name = state.atoms().name(selection);
  if (!name) return error(context, request, x11::CoreErrorCode::BadAtom,
                          selection);
  if (*name != "PRIMARY" && *name != "CLIPBOARD")
    return error(context, request, x11::CoreErrorCode::BadValue, selection);
  if ((mask & ~std::uint32_t{7}) != 0)
    return error(context, request, x11::CoreErrorCode::BadValue, mask);
  return state.selections().select_xfixes(context.client_id, window, selection,
                                          mask)
             ? DispatchResult{}
             : error(context, request, x11::CoreErrorCode::BadAlloc);
}

DispatchResult create_or_set_region(ServerState& state,
                                    const DispatchContext& context,
                                    const x11::FramedRequest& request,
                                    const bool create) {
  const auto rectangles = read_rectangles(request, context.byte_order);
  if (!rectangles)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{};
  (void)reader.read_u32(xid);
  const auto status = create
                          ? state.resources().create_xfixes_region(
                                context.client_id, context.resource_base,
                                context.resource_mask, xid, *rectangles)
                          : state.resources().set_xfixes_region(xid,
                                                                *rectangles);
  return region_status(status, context, request, xid);
}

DispatchResult one_region(ServerState& state, const DispatchContext& context,
                          const x11::FramedRequest& request,
                          const std::uint8_t operation) {
  if (request.core_size() != 8)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{};
  (void)reader.read_u32(xid);
  if (operation == kDestroyRegion)
    return region_status(state.resources().destroy_xfixes_region(xid), context,
                         request, xid);
  const auto* region = state.resources().find_xfixes_region(xid);
  if (!region) return bad_region(context, request, xid);
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  const auto extents = region_extents(region->rectangles);
  for (const auto value : {static_cast<std::uint16_t>(extents.x),
                           static_cast<std::uint16_t>(extents.y),
                           static_cast<std::uint16_t>(extents.width),
                           static_cast<std::uint16_t>(extents.height)})
    reply.write_u16(value);
  reply.write_padding(16);
  for (const auto rectangle : region->rectangles) {
    reply.write_payload_u16(static_cast<std::uint16_t>(rectangle.x));
    reply.write_payload_u16(static_cast<std::uint16_t>(rectangle.y));
    reply.write_payload_u16(static_cast<std::uint16_t>(rectangle.width));
    reply.write_payload_u16(static_cast<std::uint16_t>(rectangle.height));
  }
  return {std::move(reply).finish()};
}

DispatchResult copy_or_extents(ServerState& state,
                               const DispatchContext& context,
                               const x11::FramedRequest& request,
                               const bool extents) {
  if (request.core_size() != 12)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t source{}, destination{};
  (void)reader.read_u32(source);
  (void)reader.read_u32(destination);
  if (!state.resources().find_xfixes_region(source))
    return bad_region(context, request, source);
  if (!state.resources().find_xfixes_region(destination))
    return bad_region(context, request, destination);
  const auto status = extents
                          ? state.resources().extents_xfixes_region(
                                source, destination)
                          : state.resources().copy_xfixes_region(source,
                                                                  destination);
  return region_status(status, context, request, destination);
}

DispatchResult combine(ServerState& state, const DispatchContext& context,
                       const x11::FramedRequest& request,
                       const std::uint8_t operation) {
  if (request.core_size() != 16)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t first{}, second{}, destination{};
  (void)reader.read_u32(first);
  (void)reader.read_u32(second);
  (void)reader.read_u32(destination);
  for (const auto xid : {first, second, destination})
    if (!state.resources().find_xfixes_region(xid))
      return bad_region(context, request, xid);
  return region_status(state.resources().combine_xfixes_regions(
                           first, second, destination, operation),
                       context, request, destination);
}

DispatchResult translate(ServerState& state, const DispatchContext& context,
                         const x11::FramedRequest& request) {
  if (request.core_size() != 12)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{};
  std::uint16_t dx{}, dy{};
  (void)reader.read_u32(xid);
  (void)reader.read_u16(dx);
  (void)reader.read_u16(dy);
  if (!state.resources().find_xfixes_region(xid))
    return bad_region(context, request, xid);
  return region_status(state.resources().translate_xfixes_region(
                           xid, static_cast<std::int16_t>(dx),
                           static_cast<std::int16_t>(dy)),
                       context, request, xid);
}

DispatchResult dispatch_xfixes(ServerState& state,
                               const DispatchContext& context,
                               const x11::FramedRequest& request) {
  switch (request.data) {
    case kQueryVersion: return xfixes_query_version(context, request);
    case kSelectSelectionInput:
      return select_selection(state, context, request);
    case kCreateRegion: return create_or_set_region(state, context, request, true);
    case kDestroyRegion:
    case kFetchRegion: return one_region(state, context, request, request.data);
    case kSetRegion: return create_or_set_region(state, context, request, false);
    case kCopyRegion: return copy_or_extents(state, context, request, false);
    case kUnionRegion: return combine(state, context, request, 0);
    case kIntersectRegion: return combine(state, context, request, 1);
    case kSubtractRegion: return combine(state, context, request, 2);
    case kTranslateRegion: return translate(state, context, request);
    case kRegionExtents: return copy_or_extents(state, context, request, true);
    default: return error(context, request, x11::CoreErrorCode::BadRequest);
  }
}

}  // namespace glasswyrm::server::extensions
