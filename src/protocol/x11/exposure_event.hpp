#pragma once

#include "protocol/x11/byte_order.hpp"

#include <cstdint>
#include <vector>

namespace gw::protocol::x11 {
struct ExposeEvent { std::uint32_t window{}; std::uint16_t x{}, y{}, width{}, height{}, count{}; };
struct GraphicsExposeEvent { std::uint32_t drawable{}; std::uint16_t x{}, y{}, width{}, height{}, minor_opcode{}, count{}; std::uint8_t major_opcode{}; };
struct NoExposeEvent { std::uint32_t drawable{}; std::uint16_t minor_opcode{}; std::uint8_t major_opcode{}; };
[[nodiscard]] std::vector<std::uint8_t> encode_expose(ByteOrder, std::uint64_t, const ExposeEvent&);
[[nodiscard]] std::vector<std::uint8_t> encode_graphics_expose(ByteOrder, std::uint64_t, const GraphicsExposeEvent&);
[[nodiscard]] std::vector<std::uint8_t> encode_no_expose(ByteOrder, std::uint64_t, const NoExposeEvent&);
}  // namespace gw::protocol::x11
