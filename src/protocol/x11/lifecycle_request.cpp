#include "protocol/x11/lifecycle_request.hpp"

#include "protocol/x11/byte_cursor.hpp"

#include <bit>

namespace gw::protocol::x11 {
namespace {

bool valid_header(std::span<const std::uint8_t> bytes, ByteOrder order,
                  CoreOpcode opcode, std::uint16_t expected_units) {
  if (bytes.size() != static_cast<std::size_t>(expected_units) * 4U ||
      bytes.empty() || bytes[0] != static_cast<std::uint8_t>(opcode)) return false;
  ByteReader reader(bytes.subspan(2, 2), order);
  std::uint16_t units = 0;
  return reader.read_u16(units) && units == expected_units;
}

LifecycleDecodeStatus decode_window(std::span<const std::uint8_t> bytes,
                                    ByteOrder order, CoreOpcode opcode,
                                    WindowLifecycleRequest& request) {
  if (!valid_header(bytes, order, opcode, 2))
    return LifecycleDecodeStatus::BadLength;
  ByteReader reader(bytes.subspan(4), order);
  WindowLifecycleRequest decoded;
  if (!reader.read_u32(decoded.window) || reader.remaining() != 0)
    return LifecycleDecodeStatus::BadLength;
  request = decoded;
  return LifecycleDecodeStatus::Complete;
}

}  // namespace

LifecycleDecodeStatus decode_map_window(std::span<const std::uint8_t> bytes,
                                        ByteOrder order,
                                        WindowLifecycleRequest& request) noexcept {
  return decode_window(bytes, order, CoreOpcode::MapWindow, request);
}

LifecycleDecodeStatus decode_unmap_window(std::span<const std::uint8_t> bytes,
                                          ByteOrder order,
                                          WindowLifecycleRequest& request) noexcept {
  return decode_window(bytes, order, CoreOpcode::UnmapWindow, request);
}

LifecycleDecodeStatus decode_configure_window(
    std::span<const std::uint8_t> bytes, ByteOrder order,
    ConfigureWindowRequest& request) noexcept {
  if (bytes.size() < 12 || bytes[0] != static_cast<std::uint8_t>(CoreOpcode::ConfigureWindow))
    return LifecycleDecodeStatus::BadLength;
  ByteReader header(bytes, order);
  std::uint16_t units = 0, mask = 0;
  ConfigureWindowRequest decoded;
  if (!header.skip(2) || !header.read_u16(units) ||
      !header.read_u32(decoded.window) ||
      !header.read_u16(mask) || !header.skip(2)) return LifecycleDecodeStatus::BadLength;
  if ((mask & ~kKnownConfigureMask) != 0) return LifecycleDecodeStatus::BadValue;
  const auto values = static_cast<std::size_t>(std::popcount(mask));
  if (units != 3U + values || bytes.size() != static_cast<std::size_t>(units) * 4U)
    return LifecycleDecodeStatus::BadLength;
  decoded.value_mask = mask;
  auto read_value = [&](std::uint32_t& value) { return header.read_u32(value); };
  std::uint32_t value = 0;
  if ((mask & ConfigureX) != 0) { if (!read_value(value)) return LifecycleDecodeStatus::BadLength; decoded.x = std::bit_cast<std::int32_t>(value); }
  if ((mask & ConfigureY) != 0) { if (!read_value(value)) return LifecycleDecodeStatus::BadLength; decoded.y = std::bit_cast<std::int32_t>(value); }
  if ((mask & ConfigureWidth) != 0) { if (!read_value(value)) return LifecycleDecodeStatus::BadLength; if (value == 0) return LifecycleDecodeStatus::BadValue; decoded.width = value; }
  if ((mask & ConfigureHeight) != 0) { if (!read_value(value)) return LifecycleDecodeStatus::BadLength; if (value == 0) return LifecycleDecodeStatus::BadValue; decoded.height = value; }
  if ((mask & ConfigureBorderWidth) != 0) { if (!read_value(value)) return LifecycleDecodeStatus::BadLength; decoded.border_width = value; }
  if ((mask & ConfigureSibling) != 0) { if (!read_value(value)) return LifecycleDecodeStatus::BadLength; decoded.sibling = value; }
  if ((mask & ConfigureStackMode) != 0) {
    if (!read_value(value)) return LifecycleDecodeStatus::BadLength;
    if (value > static_cast<std::uint32_t>(CoreStackMode::Opposite))
      return LifecycleDecodeStatus::BadValue;
    decoded.stack_mode = static_cast<CoreStackMode>(value);
  }
  if (decoded.sibling && !decoded.stack_mode) return LifecycleDecodeStatus::BadMatch;
  if (decoded.sibling && *decoded.sibling == decoded.window)
    return LifecycleDecodeStatus::BadMatch;
  if (header.remaining() != 0) return LifecycleDecodeStatus::BadLength;
  request = decoded;
  return LifecycleDecodeStatus::Complete;
}

}  // namespace gw::protocol::x11
