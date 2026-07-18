#include "glasswyrmd/extensions/randr_internal.hpp"

#include "glasswyrmd/extension_registry.hpp"
#include "glasswyrmd/extension_wire.hpp"
#include "glasswyrmd/randr_state.hpp"
#include "glasswyrmd/request_handlers/common.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/reply.hpp"

#include <algorithm>
#include <limits>
#include <string>

namespace glasswyrm::server::extensions {
namespace x11 = gw::protocol::x11;
using request_handlers::error;

namespace {

constexpr std::string_view kOutputName{"Glasswyrm-1"};

DispatchResult randr_object_error(const DispatchContext& context,
                                  const x11::FramedRequest& request,
                                  const std::uint8_t relative_error,
                                  const std::uint32_t xid) {
  const auto* extension = find_extension(ExtensionKind::RandR);
  const auto packet = extension
                          ? encode_extension_error(
                                context.byte_order, *extension, relative_error,
                                context.sequence, xid, request.opcode,
                                request.data)
                          : std::nullopt;
  return packet ? DispatchResult{*packet}
                : error(context, request,
                        x11::CoreErrorCode::BadImplementation);
}

bool valid_window(const ServerState& state, const std::uint32_t window) {
  return state.resources().find_window(window) != nullptr;
}

std::uint16_t refresh_hertz(const x11::ScreenModel& screen) {
  return static_cast<std::uint16_t>(std::min<std::uint32_t>(
      std::numeric_limits<std::uint16_t>::max(),
      (screen.refresh_millihertz + 500U) / 1000U));
}

std::string mode_name(const x11::ScreenModel& screen) {
  return std::to_string(screen.width_pixels) + "x" +
         std::to_string(screen.height_pixels);
}

std::uint32_t mode_dot_clock(const x11::ScreenModel& screen) {
  const std::uint64_t clock =
      static_cast<std::uint64_t>(screen.width_pixels) * screen.height_pixels *
      screen.refresh_millihertz / 1000U;
  return static_cast<std::uint32_t>(std::min<std::uint64_t>(
      clock, std::numeric_limits<std::uint32_t>::max()));
}

}  // namespace

DispatchResult randr_bad_output(const DispatchContext& context,
                                const x11::FramedRequest& request,
                                const std::uint32_t xid) {
  return randr_object_error(context, request, 0, xid);
}

DispatchResult randr_bad_crtc(const DispatchContext& context,
                              const x11::FramedRequest& request,
                              const std::uint32_t xid) {
  return randr_object_error(context, request, 1, xid);
}

DispatchResult randr_bad_mode(const DispatchContext& context,
                              const x11::FramedRequest& request,
                              const std::uint32_t xid) {
  return randr_object_error(context, request, 2, xid);
}

DispatchResult randr_query_version(const DispatchContext& context,
                                   const x11::FramedRequest& request) {
  if (request.core_size() != 12)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u32(1);
  reply.write_u32(3);
  reply.write_padding(16);
  return {std::move(reply).finish()};
}

DispatchResult randr_select_input(ServerState& state,
                                  const DispatchContext& context,
                                  const x11::FramedRequest& request) {
  if (request.core_size() != 12)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t window{};
  std::uint16_t mask{};
  (void)reader.read_u32(window);
  (void)reader.read_u16(mask);
  if (!valid_window(state, window))
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  if ((mask & ~kRandRSupportedNotifyMask) != 0)
    return error(context, request, x11::CoreErrorCode::BadValue, mask);
  return state.randr().select(context.client_id, window, mask)
             ? DispatchResult{}
             : error(context, request, x11::CoreErrorCode::BadAlloc);
}

DispatchResult randr_get_screen_info(ServerState& state,
                                     const DispatchContext& context,
                                     const x11::FramedRequest& request) {
  if (request.core_size() != 8)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t window{};
  (void)reader.read_u32(window);
  if (!valid_window(state, window))
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  const auto& screen = state.screen();
  const auto rate = refresh_hertz(screen);
  x11::ReplyBuilder reply(context.byte_order, context.sequence, kRandRRotate0);
  reply.write_u32(screen.root_window);
  reply.write_u32(kRandRConfigurationTimestamp);
  reply.write_u32(kRandRConfigurationTimestamp);
  reply.write_u16(1);
  reply.write_u16(0);
  reply.write_u16(kRandRRotate0);
  reply.write_u16(rate);
  reply.write_u16(2);
  reply.write_padding(2);
  reply.write_payload_u16(screen.width_pixels);
  reply.write_payload_u16(screen.height_pixels);
  reply.write_payload_u16(screen.width_millimeters);
  reply.write_payload_u16(screen.height_millimeters);
  reply.write_payload_u16(1);
  reply.write_payload_u16(rate);
  return {std::move(reply).finish()};
}

DispatchResult randr_get_screen_size_range(
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
  reply.write_u16(state.screen().width_pixels);
  reply.write_u16(state.screen().height_pixels);
  reply.write_u16(state.screen().width_pixels);
  reply.write_u16(state.screen().height_pixels);
  reply.write_padding(16);
  return {std::move(reply).finish()};
}

DispatchResult randr_get_screen_resources(
    ServerState& state, const DispatchContext& context,
    const x11::FramedRequest& request) {
  if (request.core_size() != 8)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t window{};
  (void)reader.read_u32(window);
  if (!valid_window(state, window))
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  const auto& screen = state.screen();
  const auto name = mode_name(screen);
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u32(kRandRConfigurationTimestamp);
  reply.write_u32(kRandRConfigurationTimestamp);
  reply.write_u16(1);
  reply.write_u16(1);
  reply.write_u16(1);
  reply.write_u16(static_cast<std::uint16_t>(name.size()));
  reply.write_padding(8);
  reply.write_payload_u32(kRandRCrtcId);
  reply.write_payload_u32(kRandROutputId);
  reply.write_payload_u32(kRandRModeId);
  reply.write_payload_u16(screen.width_pixels);
  reply.write_payload_u16(screen.height_pixels);
  reply.write_payload_u32(mode_dot_clock(screen));
  reply.write_payload_u16(screen.width_pixels);
  reply.write_payload_u16(screen.width_pixels);
  reply.write_payload_u16(screen.width_pixels);
  reply.write_payload_u16(0);
  reply.write_payload_u16(screen.height_pixels);
  reply.write_payload_u16(screen.height_pixels);
  reply.write_payload_u16(screen.height_pixels);
  reply.write_payload_u16(static_cast<std::uint16_t>(name.size()));
  reply.write_payload_u32(0);
  reply.write_payload(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(name.data()), name.size()));
  return {std::move(reply).finish()};
}

DispatchResult randr_get_output_info(ServerState& state,
                                     const DispatchContext& context,
                                     const x11::FramedRequest& request) {
  if (request.core_size() != 12)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t output{}, config_timestamp{};
  (void)reader.read_u32(output);
  (void)reader.read_u32(config_timestamp);
  if (output != kRandROutputId)
    return randr_bad_output(context, request, output);
  static_cast<void>(config_timestamp);
  x11::ByteWriter writer(context.byte_order);
  writer.write_u8(1);
  writer.write_u8(0);
  writer.write_u16(x11::wire_sequence(context.sequence));
  writer.write_u32(6);
  writer.write_u32(kRandRConfigurationTimestamp);
  writer.write_u32(kRandRCrtcId);
  writer.write_u32(state.screen().width_millimeters);
  writer.write_u32(state.screen().height_millimeters);
  writer.write_u8(0);
  writer.write_u8(0);
  writer.write_u16(1);
  writer.write_u16(1);
  writer.write_u16(1);
  writer.write_u16(0);
  writer.write_u16(static_cast<std::uint16_t>(kOutputName.size()));
  writer.write_u32(kRandRCrtcId);
  writer.write_u32(kRandRModeId);
  writer.write_bytes(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(kOutputName.data()),
      kOutputName.size()));
  writer.write_padding(1);
  return {std::move(writer).take()};
}

}  // namespace glasswyrm::server::extensions
