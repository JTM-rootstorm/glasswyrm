#include "glasswyrmd/extensions/render_internal.hpp"

#include "glasswyrmd/extension_registry.hpp"
#include "glasswyrmd/extension_wire.hpp"
#include "glasswyrmd/picture.hpp"
#include "glasswyrmd/request_handlers/common.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/reply.hpp"

#include <algorithm>

namespace glasswyrm::server::extensions {
namespace x11 = gw::protocol::x11;
using request_handlers::error;

DispatchResult render_extension_error(const DispatchContext& context,
                                      const x11::FramedRequest& request,
                                      const std::uint8_t relative_error,
                                      const std::uint32_t bad_value) {
  const auto* extension = find_extension(ExtensionKind::Render);
  const auto packet =
      extension ? encode_extension_error(
                      context.byte_order, *extension, relative_error,
                      context.sequence, bad_value, request.opcode, request.data)
                : std::nullopt;
  return packet ? DispatchResult{*packet}
                : error(context, request,
                        x11::CoreErrorCode::BadImplementation);
}

DispatchResult render_query_version(const DispatchContext& context,
                                    const x11::FramedRequest& request) {
  if (request.core_size() != 12)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t major{}, minor{};
  (void)reader.read_u32(major);
  (void)reader.read_u32(minor);
  if (major != 0) {
    major = 0;
    minor = 11;
  } else {
    minor = std::min(minor, std::uint32_t{11});
  }
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u32(major);
  reply.write_u32(minor);
  reply.write_padding(16);
  return {std::move(reply).finish()};
}

DispatchResult render_query_formats(const ServerState& state,
                                    const DispatchContext& context,
                                    const x11::FramedRequest& request) {
  if (request.core_size() != 4)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u32(4);
  reply.write_u32(1);
  reply.write_u32(4);
  reply.write_u32(1);
  reply.write_u32(1);
  reply.write_padding(4);
  x11::ByteWriter payload(context.byte_order);
  for (const auto& format : kCanonicalPictureFormats) {
    payload.write_u32(static_cast<std::uint32_t>(format.id));
    payload.write_u8(1);
    payload.write_u8(format.depth);
    payload.write_padding(2);
    payload.write_u16(format.red_shift);
    payload.write_u16(format.red_mask);
    payload.write_u16(format.green_shift);
    payload.write_u16(format.green_mask);
    payload.write_u16(format.blue_shift);
    payload.write_u16(format.blue_mask);
    payload.write_u16(format.alpha_shift);
    payload.write_u16(format.alpha_mask);
    payload.write_u32(0);
  }
  payload.write_u32(4);
  payload.write_u32(static_cast<std::uint32_t>(PictureFormatId::Argb32));
  const auto write_depth = [&](const std::uint8_t depth,
                               const std::uint16_t visual_count) {
    payload.write_u8(depth);
    payload.write_padding(1);
    payload.write_u16(visual_count);
    payload.write_padding(4);
    if (visual_count != 0) {
      payload.write_u32(state.screen().root_visual);
      payload.write_u32(
          static_cast<std::uint32_t>(PictureFormatId::Xrgb32));
    }
  };
  write_depth(1, 0);
  write_depth(8, 0);
  write_depth(24, 1);
  write_depth(32, 0);
  payload.write_u32(0);
  reply.write_payload(std::move(payload).take());
  return {std::move(reply).finish()};
}

DispatchResult render_query_index_values(
    const DispatchContext& context, const x11::FramedRequest& request) {
  if (request.core_size() != 8)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t raw_format{};
  (void)reader.read_u32(raw_format);
  if (!find_picture_format(static_cast<PictureFormatId>(raw_format)))
    return render_extension_error(context, request, 0, raw_format);
  return error(context, request, x11::CoreErrorCode::BadMatch, raw_format);
}

}  // namespace glasswyrm::server::extensions
