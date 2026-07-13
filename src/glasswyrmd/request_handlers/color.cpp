#include "glasswyrmd/request_handlers/common.hpp"

#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/reply.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace glasswyrm::server::request_handlers {
namespace x11 = gw::protocol::x11;

struct Color { std::uint16_t red{}, green{}, blue{}; };

std::optional<Color> parse_color_name(std::span<const std::uint8_t> bytes) {
  std::string name(bytes.begin(), bytes.end());
  if (!name.empty() && name.front() == '#') {
    const auto digits = name.size() - 1;
    if (digits != 3 && digits != 6 && digits != 12) return std::nullopt;
    auto nibble = [](const char value) -> std::optional<std::uint8_t> {
      if (value >= '0' && value <= '9') return value - '0';
      if (value >= 'a' && value <= 'f') return value - 'a' + 10;
      if (value >= 'A' && value <= 'F') return value - 'A' + 10;
      return std::nullopt;
    };
    std::array<std::uint16_t, 3> values{};
    const auto width = digits / 3;
    for (std::size_t component = 0; component < 3; ++component) {
      std::uint16_t value = 0;
      for (std::size_t offset = 0; offset < width; ++offset) {
        const auto parsed = nibble(name[1 + component * width + offset]);
        if (!parsed) return std::nullopt;
        value = static_cast<std::uint16_t>((value << 4U) | *parsed);
      }
      values[component] = width == 1 ? static_cast<std::uint16_t>(value * 0x1111U)
                        : width == 2 ? static_cast<std::uint16_t>(value * 0x0101U)
                                     : value;
    }
    return Color{values[0], values[1], values[2]};
  }
  std::ranges::transform(name, name.begin(), [](const unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  if (name == "black") return Color{0, 0, 0};
  if (name == "white") return Color{0xffff, 0xffff, 0xffff};
  if (name == "red") return Color{0xffff, 0, 0};
  if (name == "green") return Color{0, 0xffff, 0};
  if (name == "blue") return Color{0, 0, 0xffff};
  if (name == "yellow") return Color{0xffff, 0xffff, 0};
  if (name == "cyan") return Color{0, 0xffff, 0xffff};
  if (name == "magenta") return Color{0xffff, 0, 0xffff};
  if (name == "gray" || name == "grey") return Color{0x8080, 0x8080, 0x8080};
  if (name == "light gray" || name == "light grey") return Color{0xd3d3, 0xd3d3, 0xd3d3};
  if (name == "dark gray" || name == "dark grey") return Color{0xa9a9, 0xa9a9, 0xa9a9};
  return std::nullopt;
}

Color quantize_color(const Color color) {
  return {static_cast<std::uint16_t>((color.red >> 8U) * 257U),
          static_cast<std::uint16_t>((color.green >> 8U) * 257U),
          static_cast<std::uint16_t>((color.blue >> 8U) * 257U)};
}
std::uint32_t color_pixel(const Color color) {
  return (static_cast<std::uint32_t>(color.red >> 8U) << 16U) |
         (static_cast<std::uint32_t>(color.green >> 8U) << 8U) |
         (color.blue >> 8U);
}

DispatchResult alloc_color(const ServerState& state, const DispatchContext& context,
                           const x11::FramedRequest& request) {
  if (!exact_size(request, 16)) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t colormap{}; Color color{};
  if (!reader.read_u32(colormap) || !reader.read_u16(color.red) ||
      !reader.read_u16(color.green) || !reader.read_u16(color.blue) || !reader.skip(2))
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (colormap != state.screen().default_colormap)
    return error(context, request, x11::CoreErrorCode::BadColormap, colormap);
  color = quantize_color(color);
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u16(color.red); reply.write_u16(color.green); reply.write_u16(color.blue);
  reply.write_padding(2); reply.write_u32(color_pixel(color));
  return {std::move(reply).finish()};
}

DispatchResult named_color(const ServerState& state, const DispatchContext& context,
                           const x11::FramedRequest& request, const bool allocate) {
  if (request.bytes.size() < 12) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t colormap{}; std::uint16_t name_length{};
  if (!reader.read_u32(colormap) || !reader.read_u16(name_length) || !reader.skip(2) ||
      !exact_size(request, 12 + padded_size(name_length)))
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (colormap != state.screen().default_colormap)
    return error(context, request, x11::CoreErrorCode::BadColormap, colormap);
  std::span<const std::uint8_t> name;
  if (!reader.read_bytes(name_length, name)) return error(context, request, x11::CoreErrorCode::BadLength);
  const auto parsed = parse_color_name(name);
  if (!parsed) return error(context, request, x11::CoreErrorCode::BadName);
  const auto color = quantize_color(*parsed);
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  if (allocate) reply.write_u32(color_pixel(color));
  reply.write_u16(color.red); reply.write_u16(color.green); reply.write_u16(color.blue);
  reply.write_u16(color.red); reply.write_u16(color.green); reply.write_u16(color.blue);
  return {std::move(reply).finish()};
}

DispatchResult free_colors(const ServerState& state, const DispatchContext& context,
                           const x11::FramedRequest& request) {
  if (request.bytes.size() < 12 || request.bytes.size() % 4 != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order); std::uint32_t colormap{};
  if (!reader.read_u32(colormap)) return error(context, request, x11::CoreErrorCode::BadLength);
  if (colormap != state.screen().default_colormap)
    return error(context, request, x11::CoreErrorCode::BadColormap, colormap);
  return {};
}

DispatchResult query_colors(const ServerState& state, const DispatchContext& context,
                            const x11::FramedRequest& request) {
  if (request.bytes.size() < 8 || request.bytes.size() % 4 != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order); std::uint32_t colormap{};
  if (!reader.read_u32(colormap)) return error(context, request, x11::CoreErrorCode::BadLength);
  if (colormap != state.screen().default_colormap)
    return error(context, request, x11::CoreErrorCode::BadColormap, colormap);
  const auto count = (request.bytes.size() - 8) / 4;
  if (count > std::numeric_limits<std::uint16_t>::max()) return error(context, request, x11::CoreErrorCode::BadAlloc);
  x11::ReplyBuilder reply(context.byte_order, context.sequence); reply.write_u16(static_cast<std::uint16_t>(count)); reply.write_padding(22);
  for (std::size_t i = 0; i < count; ++i) { std::uint32_t pixel{}; (void)reader.read_u32(pixel); reply.write_payload_u16(static_cast<std::uint16_t>(((pixel >> 16U) & 0xffU) * 257U)); reply.write_payload_u16(static_cast<std::uint16_t>(((pixel >> 8U) & 0xffU) * 257U)); reply.write_payload_u16(static_cast<std::uint16_t>((pixel & 0xffU) * 257U)); reply.write_payload_u16(0); }
  return {std::move(reply).finish()};
}


}  // namespace glasswyrm::server::request_handlers
