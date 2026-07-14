#include "glasswyrmd/request_handlers/common.hpp"

#include "glasswyrmd/font.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/reply.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace glasswyrm::server::request_handlers {
namespace x11 = gw::protocol::x11;

bool valid_fontable(const ResourceTable& resources, const std::uint32_t xid) {
  return resources.find_font(xid) || resources.find_gc(xid);
}

DispatchResult open_font(ServerState& state, const DispatchContext& context,
                         const x11::FramedRequest& request) {
  if (request.bytes.size() < 12) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{}; std::uint16_t name_length{};
  (void)reader.read_u32(xid); (void)reader.read_u16(name_length); (void)reader.skip(2);
  const auto padded = (static_cast<std::size_t>(name_length) + 3U) & ~std::size_t{3U};
  if (!exact_size(request, 12U + padded))
    return error(context, request, x11::CoreErrorCode::BadLength);
  const auto name = std::string_view(
      reinterpret_cast<const char*>(request.bytes.data() + 12), name_length);
  if (!matches_fixed_font(name))
    return error(context, request, x11::CoreErrorCode::BadName);
  switch (state.resources().open_font(context.client_id, context.resource_base,
                                      context.resource_mask, xid)) {
    case OpenFontStatus::Success: return {};
    case OpenFontStatus::BadIdChoice:
      return error(context, request, x11::CoreErrorCode::BadIDChoice, xid);
    case OpenFontStatus::BadAlloc:
      return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return {};
}

DispatchResult close_font(ServerState& state, const DispatchContext& context,
                          const x11::FramedRequest& request) {
  if (!exact_size(request, 8)) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{}; (void)reader.read_u32(xid);
  return state.resources().close_font(xid) == CloseFontStatus::Success
      ? DispatchResult{} : error(context, request, x11::CoreErrorCode::BadFont, xid);
}

void write_char_info(x11::ByteWriter& writer) {
  writer.write_u16(0); writer.write_u16(kFixedFontAdvance);
  writer.write_u16(kFixedFontAdvance); writer.write_u16(kFixedFontAscent);
  writer.write_u16(kFixedFontDescent); writer.write_u16(0);
}

DispatchResult query_font(ServerState& state, const DispatchContext& context,
                          const x11::FramedRequest& request) {
  if (!exact_size(request, 8)) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{}; (void)reader.read_u32(xid);
  if (!valid_fontable(state.resources(), xid))
    return error(context, request, x11::CoreErrorCode::BadFont, xid);
  x11::ByteWriter reply(context.byte_order);
  reply.write_u8(1); reply.write_u8(0); reply.write_u16(x11::wire_sequence(context.sequence));
  constexpr std::uint32_t characters = kFixedFontLastCharacter - kFixedFontFirstCharacter + 1U;
  constexpr std::uint32_t extra_bytes = 60U - 32U + characters * 12U;
  reply.write_u32(extra_bytes / 4U);
  write_char_info(reply); reply.write_padding(4); write_char_info(reply); reply.write_padding(4);
  reply.write_u16(kFixedFontFirstCharacter); reply.write_u16(kFixedFontLastCharacter);
  reply.write_u16(kFixedFontDefaultCharacter); reply.write_u16(0);
  reply.write_u8(0); reply.write_u8(0); reply.write_u8(0); reply.write_u8(1);
  reply.write_u16(kFixedFontAscent); reply.write_u16(kFixedFontDescent);
  reply.write_u32(characters);
  for (std::uint32_t index = 0; index < characters; ++index) write_char_info(reply);
  return {std::move(reply).take()};
}

DispatchResult query_text_extents(ServerState& state,
                                  const DispatchContext& context,
                                  const x11::FramedRequest& request) {
  if (request.bytes.size() < 8 || (request.bytes.size() & 3U) != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{}; (void)reader.read_u32(xid);
  if (!valid_fontable(state.resources(), xid))
    return error(context, request, x11::CoreErrorCode::BadFont, xid);
  const auto bytes_after_font = request.bytes.size() - 8U;
  if ((bytes_after_font & 1U) != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  const std::size_t characters = bytes_after_font / 2U - (request.data ? 1U : 0U);
  if (request.data > 1 || characters * 2U + (request.data ? 2U : 0U) != bytes_after_font)
    return error(context, request, x11::CoreErrorCode::BadLength);
  for (std::size_t index = 0; index < characters; ++index) {
    std::uint8_t byte1{}, byte2{}; (void)reader.read_u8(byte1); (void)reader.read_u8(byte2);
    if (byte1 != 0) return error(context, request, x11::CoreErrorCode::BadImplementation);
  }
  x11::ReplyBuilder reply(context.byte_order, context.sequence, 0);
  reply.write_u16(kFixedFontAscent); reply.write_u16(kFixedFontDescent);
  reply.write_u16(kFixedFontAscent); reply.write_u16(kFixedFontDescent);
  const auto width = static_cast<std::uint32_t>(characters * kFixedFontAdvance);
  reply.write_u32(width); reply.write_u32(0); reply.write_u32(width);
  reply.write_padding(4);
  return {std::move(reply).finish()};
}

DispatchResult list_fonts(const DispatchContext& context,
                          const x11::FramedRequest& request) {
  if (request.bytes.size() < 8) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint16_t maximum{}, pattern_length{};
  (void)reader.read_u16(maximum); (void)reader.read_u16(pattern_length);
  const auto padded = (static_cast<std::size_t>(pattern_length) + 3U) & ~std::size_t{3U};
  if (!exact_size(request, 8U + padded))
    return error(context, request, x11::CoreErrorCode::BadLength);
  const auto pattern = std::string_view(
      reinterpret_cast<const char*>(request.bytes.data() + 8), pattern_length);
  const bool match = maximum != 0 && matches_fixed_font(pattern);
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u16(match ? 1 : 0); reply.write_padding(22);
  if (match) {
    constexpr std::array<std::uint8_t, 6> fixed_name{'\x05','f','i','x','e','d'};
    reply.write_payload(fixed_name);
  }
  return {std::move(reply).finish()};
}


}  // namespace glasswyrm::server::request_handlers
