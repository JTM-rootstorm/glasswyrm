#include "protocol/x11/focus_event.hpp"

#include "protocol/x11/byte_cursor.hpp"

#include <cassert>

namespace gw::protocol::x11 {
std::vector<std::uint8_t> encode_focus_event(
    const ByteOrder order, const std::uint64_t sequence, const FocusEvent& e) {
  assert(e.type == CoreEventType::FocusIn || e.type == CoreEventType::FocusOut);
  ByteWriter w(order); w.write_u8(static_cast<std::uint8_t>(e.type));
  w.write_u8(static_cast<std::uint8_t>(e.detail)); w.write_u16(wire_sequence(sequence));
  w.write_u32(e.event); w.write_u8(static_cast<std::uint8_t>(e.mode)); w.write_padding(23);
  assert(w.size() == 32); return std::move(w).take();
}
}  // namespace gw::protocol::x11
