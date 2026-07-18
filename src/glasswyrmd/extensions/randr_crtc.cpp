#include "glasswyrmd/extensions/randr_internal.hpp"

#include "glasswyrmd/randr_state.hpp"
#include "glasswyrmd/request_handlers/common.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/reply.hpp"

#include <ranges>

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

bool valid_output(const ServerState& state, const std::uint32_t output) {
  return state.randr().output_model_enabled()
             ? state.randr().find_output(output) != nullptr
             : output == kRandROutputId;
}

std::uint16_t randr_rotation(
    const glasswyrm::output::OutputTransform transform) noexcept {
  switch (transform) {
    case glasswyrm::output::OutputTransform::Normal: return 1;
    case glasswyrm::output::OutputTransform::Rotate90: return 2;
    case glasswyrm::output::OutputTransform::Rotate180: return 4;
    case glasswyrm::output::OutputTransform::Rotate270: return 8;
    case glasswyrm::output::OutputTransform::Flipped: return 33;
    case glasswyrm::output::OutputTransform::Flipped90: return 34;
    case glasswyrm::output::OutputTransform::Flipped180: return 36;
    case glasswyrm::output::OutputTransform::Flipped270: return 40;
  }
  return kRandRRotate0;
}

const RandRModeObject* current_mode(const RandROutputObject& output) noexcept {
  const auto found =
      std::ranges::find(output.modes, true, &RandRModeObject::current);
  return found == output.modes.end() ? nullptr : &*found;
}

}  // namespace

DispatchResult randr_list_output_properties(
    ServerState& state, const DispatchContext& context,
    const x11::FramedRequest& request) {
  if (request.core_size() != 8)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t output{};
  (void)reader.read_u32(output);
  if (!valid_output(state, output))
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
  if (!valid_output(state, output))
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
  if (!valid_output(state, output))
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
  if (state.randr().output_model_enabled()) {
    const auto* output = state.randr().find_crtc(crtc);
    if (!output || !output->enabled)
      return randr_bad_crtc(context, request, crtc);
    static_cast<void>(config_timestamp);
    const auto* mode = current_mode(*output);
    if (!mode) return randr_bad_crtc(context, request, crtc);
    const auto rotation = randr_rotation(output->transform);
    x11::ReplyBuilder reply(context.byte_order, context.sequence, 0);
    reply.write_u32(state.randr().configuration_timestamp());
    reply.write_u16(static_cast<std::uint16_t>(output->logical_x));
    reply.write_u16(static_cast<std::uint16_t>(output->logical_y));
    reply.write_u16(static_cast<std::uint16_t>(output->logical_width));
    reply.write_u16(static_cast<std::uint16_t>(output->logical_height));
    reply.write_u32(mode->xid);
    reply.write_u16(rotation);
    reply.write_u16(0x003f);
    reply.write_u16(1);
    reply.write_u16(1);
    reply.write_payload_u32(output->xid);
    reply.write_payload_u32(output->xid);
    return {std::move(reply).finish()};
  }
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
    ServerState& state, const DispatchContext& context,
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
  const auto output_count = (request.core_size() - 28U) / 4U;
  std::uint32_t output{};
  bool outputs_valid = true;
  for (std::size_t index = 0; index < output_count; ++index) {
    (void)reader.read_u32(output);
    if (!valid_output(state, output)) {
      outputs_valid = false;
      break;
    }
  }
  if (!outputs_valid) return randr_bad_output(context, request, output);
  if (state.randr().output_model_enabled()) {
    const auto* object = state.randr().find_crtc(crtc);
    if (!object || !object->enabled)
      return randr_bad_crtc(context, request, crtc);
    if (mode != 0 && !state.randr().find_mode(mode))
      return randr_bad_mode(context, request, mode);
    const auto* current = current_mode(*object);
    const bool idempotent =
        current && x == object->logical_x && y == object->logical_y &&
        mode == current->xid && rotation == randr_rotation(object->transform) &&
        output_count == 1 && output == object->xid;
    x11::ReplyBuilder reply(context.byte_order, context.sequence,
                            idempotent ? 0 : 3);
    reply.write_u32(state.randr().configuration_timestamp());
    reply.write_padding(20);
    return {std::move(reply).finish()};
  }
  if (crtc != kRandRCrtcId) return randr_bad_crtc(context, request, crtc);
  if (mode != 0 && mode != kRandRModeId)
    return randr_bad_mode(context, request, mode);
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
    ServerState& state, const DispatchContext& context,
    const x11::FramedRequest& request) {
  if (request.core_size() != 8)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t crtc{};
  (void)reader.read_u32(crtc);
  if (state.randr().output_model_enabled()) {
    const auto* output = state.randr().find_crtc(crtc);
    if (!output || !output->enabled)
      return randr_bad_crtc(context, request, crtc);
  } else if (crtc != kRandRCrtcId) {
    return randr_bad_crtc(context, request, crtc);
  }
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
  reply.write_u32(state.randr().output_model_enabled()
                      ? state.randr().primary_output_xid()
                      : kRandROutputId);
  reply.write_padding(20);
  return {std::move(reply).finish()};
}

}  // namespace glasswyrm::server::extensions
