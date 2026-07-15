#include "glasswyrmd/request_handlers/common.hpp"

#include "protocol/x11/byte_cursor.hpp"

#include <bit>

namespace glasswyrm::server::request_handlers {
namespace {

std::vector<std::uint8_t>
query_keymap_reply(const DispatchContext &context) {
  x11::ByteWriter writer(context.byte_order);
  writer.write_u8(1);
  writer.write_u8(0);
  writer.write_u16(x11::wire_sequence(context.sequence));
  writer.write_u32(2);
  writer.write_bytes(context.input.keymap);
  return std::move(writer).take();
}

std::vector<std::uint8_t>
keyboard_control_reply(const DispatchContext &context,
                       const KeyboardControlState &control) {
  x11::ByteWriter writer(context.byte_order);
  writer.write_u8(1);
  writer.write_u8(control.global_auto_repeat);
  writer.write_u16(x11::wire_sequence(context.sequence));
  writer.write_u32(5);
  writer.write_u32(control.led_mask);
  writer.write_u8(control.key_click_percent);
  writer.write_u8(control.bell_percent);
  writer.write_u16(control.bell_pitch);
  writer.write_u16(control.bell_duration);
  writer.write_u16(0);
  writer.write_bytes(control.auto_repeats);
  return std::move(writer).take();
}

} // namespace

DispatchResult query_keymap(const DispatchContext &context,
                            const x11::FramedRequest &request) {
  if (!exact_size(request, 4))
    return error(context, request, x11::CoreErrorCode::BadLength);
  return {query_keymap_reply(context)};
}

DispatchResult change_keyboard_control(ServerState &state,
                                       const DispatchContext &context,
                                       const x11::FramedRequest &request) {
  if (request.bytes.size() < 8)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t mask{};
  if (!reader.read_u32(mask) || (mask & ~UINT32_C(0xff)) != 0 ||
      static_cast<std::size_t>(std::popcount(mask)) != reader.remaining() / 4U ||
      (reader.remaining() & 3U) != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  if ((mask & UINT32_C(0x70)) != 0)
    return error(context, request, x11::CoreErrorCode::BadImplementation);
  auto staged = state.keyboard_control();
  for (std::uint32_t bit = 0; bit < 8; ++bit) {
    if ((mask & (UINT32_C(1) << bit)) == 0)
      continue;
    std::uint32_t wire{};
    if (!reader.read_u32(wire))
      return error(context, request, x11::CoreErrorCode::BadLength);
    const auto value = std::bit_cast<std::int32_t>(wire);
    if (bit == 0) {
      if (value < -1 || value > 100)
        return error(context, request, x11::CoreErrorCode::BadValue, wire);
      staged.key_click_percent =
          static_cast<std::uint8_t>(value < 0 ? 0 : value);
    } else if (bit == 1) {
      if (value < -1 || value > 100)
        return error(context, request, x11::CoreErrorCode::BadValue, wire);
      staged.bell_percent =
          static_cast<std::uint8_t>(value < 0 ? 50 : value);
    } else if (bit == 2) {
      if (value < -1 || value > UINT16_MAX)
        return error(context, request, x11::CoreErrorCode::BadValue, wire);
      staged.bell_pitch =
          static_cast<std::uint16_t>(value < 0 ? 400 : value);
    } else if (bit == 3) {
      if (value < -1 || value > UINT16_MAX)
        return error(context, request, x11::CoreErrorCode::BadValue, wire);
      staged.bell_duration =
          static_cast<std::uint16_t>(value < 0 ? 100 : value);
    } else if (bit == 7) {
      if (value < 0 || value > 2)
        return error(context, request, x11::CoreErrorCode::BadValue, wire);
      if (value != 2)
        staged.global_auto_repeat = static_cast<std::uint8_t>(value);
    }
  }
  state.keyboard_control() = staged;
  return {};
}

DispatchResult get_keyboard_control(const ServerState &state,
                                    const DispatchContext &context,
                                    const x11::FramedRequest &request) {
  if (!exact_size(request, 4))
    return error(context, request, x11::CoreErrorCode::BadLength);
  return {keyboard_control_reply(context, state.keyboard_control())};
}

DispatchResult bell(const DispatchContext &context,
                    const x11::FramedRequest &request) {
  if (!exact_size(request, 4))
    return error(context, request, x11::CoreErrorCode::BadLength);
  const auto percent = std::bit_cast<std::int8_t>(request.data);
  if (percent < -100 || percent > 100)
    return error(context, request, x11::CoreErrorCode::BadValue, request.data);
  return {};
}

} // namespace glasswyrm::server::request_handlers
