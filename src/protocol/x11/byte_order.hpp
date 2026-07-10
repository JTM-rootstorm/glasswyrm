#pragma once

#include <cstdint>
#include <optional>

namespace gw::protocol::x11 {

enum class ByteOrder : std::uint8_t {
  LittleEndian = 'l',
  BigEndian = 'B',
};

[[nodiscard]] constexpr std::optional<ByteOrder>
byte_order_from_marker(const std::uint8_t marker) noexcept {
  if (marker == static_cast<std::uint8_t>(ByteOrder::LittleEndian)) {
    return ByteOrder::LittleEndian;
  }
  if (marker == static_cast<std::uint8_t>(ByteOrder::BigEndian)) {
    return ByteOrder::BigEndian;
  }
  return std::nullopt;
}

} // namespace gw::protocol::x11
