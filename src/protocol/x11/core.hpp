#pragma once

#include <cstdint>

namespace gw::protocol::x11 {

enum class CoreOpcode : std::uint8_t {
  CreateWindow = 1,
  DestroyWindow = 4,
  GetGeometry = 14,
  QueryTree = 15,
  InternAtom = 16,
  GetAtomName = 17,
  ChangeProperty = 18,
  DeleteProperty = 19,
  GetProperty = 20,
  ListProperties = 21,
  GetInputFocus = 43,
  NoOperation = 127,
};

enum class CoreErrorCode : std::uint8_t {
  BadRequest = 1,
  BadValue = 2,
  BadWindow = 3,
  BadPixmap = 4,
  BadAtom = 5,
  BadCursor = 6,
  BadFont = 7,
  BadMatch = 8,
  BadDrawable = 9,
  BadAccess = 10,
  BadAlloc = 11,
  BadColor = 12,
  BadColormap = 12,
  BadGContext = 13,
  BadIDChoice = 14,
  BadName = 15,
  BadLength = 16,
  BadImplementation = 17,
};

[[nodiscard]] constexpr std::uint16_t
wire_sequence(const std::uint64_t sequence) noexcept {
  return static_cast<std::uint16_t>(sequence & 0xffffU);
}

} // namespace gw::protocol::x11
