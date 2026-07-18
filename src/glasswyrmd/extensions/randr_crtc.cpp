#include "glasswyrmd/extensions/randr_internal.hpp"

#include "glasswyrmd/randr_state.hpp"
#include "glasswyrmd/request_handlers/common.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/reply.hpp"

namespace glasswyrm::server::extensions {
namespace x11 = gw::protocol::x11;
using request_handlers::error;

namespace {

bool valid_window(const ServerState& state, const std::uint32_t window) {
  return state.resources().find_window(window) != nullptr;
}

bool valid_atom(const ServerState& state, const std::uint32_t atom) {
  return atom != 0 && state.atoms().name(atom).has_value();
}

}  // namespace

DispatchResult randr_list_output_properties(
    ServerState&, const DispatchContext& context,
    const x11::FramedRequest& request) {
  if (request.core_size() != 8)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t output{};
  (void)reader.read_u32(output);
  if (output != kRandROutputId)
    return randr_bad_output(context, request, output);
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u16(0);
  reply.write_padding(22);
  return {std::move(reply).finish()};
}

DispatchResult randr_query_output_property(
    ServerState& state, const DispatchContext& context,
    const x11::FramedRequest& request) {
  if (request.core_size() != 12)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t output{}, property{};
  (void)reader.read_u32(output);
  (void)reader.read_u32(property);
  if (output != kRandROutputId)
    return randr_bad_output(context, request, output);
  if (!valid_atom(state, property))
    return error(context, request, x11::CoreErrorCode::BadAtom, property);
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u8(0);
  reply.write_u8(0);
  reply.write_u8(0);
  reply.write_padding(21);
  return {std::move(reply).finish()};
}

DispatchResult randr_get_output_property(
    ServerState& state, const DispatchContext& context,
    const x11::FramedRequest& request) {
  if (request.core_size() != 28)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t output{}, property{}, type{}, offset{}, length{};
  std::uint8_t remove{}, pending{};
  (void)reader.read_u32(output);
  (void)reader.read_u32(property);
  (void)reader.read_u32(type);
  (void)reader.read_u32(offset);
  (void)reader.read_u32(length);
  (void)reader.read_u8(remove);
  (void)reader.read_u8(pending);
  static_cast<void>(offset);
  static_cast<void>(length);
  if (output != kRandROutputId)
    return randr_bad_output(context, request, output);
  if (!valid_atom(state, property))
    return error(context, request, x11::CoreErrorCode::BadAtom, property);
  if (type != 0 && !valid_atom(state, type))
    return error(context, request, x11::CoreErrorCode::BadAtom, type);
  if (remove > 1 || pending > 1)
    return error(context, request, x11::CoreErrorCode::BadValue,
                 remove > 1 ? remove : pending);
  x11::ReplyBuilder reply(context.byte_order, context.sequence, 0);
  reply.write_u32(0);
  reply.write_u32(0);
  reply.write_u32(0);
  reply.write_padding(12);
  return {std::move(reply).finish()};
}

DispatchResult randr_get_crtc_info(ServerState& state,
                                   const DispatchContext& context,
                                   const x11::FramedRequest& request) {
  if (request.core_size() != 12)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t crtc{}, config_timestamp{};
  (void)reader.read_u32(crtc);
  (void)reader.read_u32(config_timestamp);
  if (crtc != kRandRCrtcId) return randr_bad_crtc(context, request, crtc);
  static_cast<void>(config_timestamp);
  x11::ReplyBuilder reply(context.byte_order, context.sequence, 0);
  reply.write_u32(kRandRConfigurationTimestamp);
  reply.write_u16(0);
  reply.write_u16(0);
  reply.write_u16(state.screen().width_pixels);
  reply.write_u16(state.screen().height_pixels);
  reply.write_u32(kRandRModeId);
  reply.write_u16(kRandRRotate0);
  reply.write_u16(kRandRRotate0);
  reply.write_u16(1);
  reply.write_u16(1);
  reply.write_payload_u32(kRandROutputId);
  reply.write_payload_u32(kRandROutputId);
  return {std::move(reply).finish()};
}

DispatchResult randr_set_crtc_config(
    ServerState&, const DispatchContext& context,
    const x11::FramedRequest& request) {
  if (request.core_size() < 28 || (request.core_size() - 28U) % 4U != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t crtc{}, timestamp{}, config_timestamp{}, mode{};
  std::uint16_t x{}, y{}, rotation{}, padding{};
  (void)reader.read_u32(crtc);
  (void)reader.read_u32(timestamp);
  (void)reader.read_u32(config_timestamp);
  (void)reader.read_u16(x);
  (void)reader.read_u16(y);
  (void)reader.read_u32(mode);
  (void)reader.read_u16(rotation);
  (void)reader.read_u16(padding);
  static_cast<void>(timestamp);
  static_cast<void>(config_timestamp);
  static_cast<void>(padding);
  if (crtc != kRandRCrtcId) return randr_bad_crtc(context, request, crtc);
  if (mode != 0 && mode != kRandRModeId)
    return randr_bad_mode(context, request, mode);
  const auto output_count = (request.core_size() - 28U) / 4U;
  std::uint32_t output{};
  for (std::size_t index = 0; index < output_count; ++index) {
    (void)reader.read_u32(output);
    if (output != kRandROutputId)
      return randr_bad_output(context, request, output);
  }
  const bool idempotent =
      x == 0 && y == 0 && mode == kRandRModeId &&
      rotation == kRandRRotate0 && output_count == 1 &&
      output == kRandROutputId;
  x11::ReplyBuilder reply(context.byte_order, context.sequence,
                          idempotent ? 0 : 3);
  reply.write_u32(kRandRConfigurationTimestamp);
  reply.write_padding(20);
  return {std::move(reply).finish()};
}

DispatchResult randr_get_crtc_gamma_size(
    ServerState&, const DispatchContext& context,
    const x11::FramedRequest& request) {
  if (request.core_size() != 8)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t crtc{};
  (void)reader.read_u32(crtc);
  if (crtc != kRandRCrtcId) return randr_bad_crtc(context, request, crtc);
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u16(0);
  reply.write_padding(22);
  return {std::move(reply).finish()};
}

DispatchResult randr_get_output_primary(
    ServerState& state, const DispatchContext& context,
    const x11::FramedRequest& request) {
  if (request.core_size() != 8)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t window{};
  (void)reader.read_u32(window);
  if (!valid_window(state, window))
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u32(kRandROutputId);
  reply.write_padding(20);
  return {std::move(reply).finish()};
}

}  // namespace glasswyrm::server::extensions
