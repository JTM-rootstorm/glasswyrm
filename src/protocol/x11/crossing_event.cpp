#include "protocol/x11/crossing_event.hpp"

#include "protocol/x11/byte_cursor.hpp"

#include <cassert>

namespace gw::protocol::x11 {
std::vector<std::uint8_t> encode_crossing_event(
    const ByteOrder order, const std::uint64_t sequence, const CrossingEvent& e) {
  assert(e.type == CoreEventType::EnterNotify || e.type == CoreEventType::LeaveNotify);
  ByteWriter w(order);
  w.write_u8(static_cast<std::uint8_t>(e.type)); w.write_u8(static_cast<std::uint8_t>(e.detail));
  w.write_u16(wire_sequence(sequence)); w.write_u32(e.time); w.write_u32(e.root);
  w.write_u32(e.event); w.write_u32(e.child);
  w.write_u16(static_cast<std::uint16_t>(e.root_x)); w.write_u16(static_cast<std::uint16_t>(e.root_y));
  w.write_u16(static_cast<std::uint16_t>(e.event_x)); w.write_u16(static_cast<std::uint16_t>(e.event_y));
  w.write_u16(e.state); w.write_u8(static_cast<std::uint8_t>(e.mode));
  w.write_u8(static_cast<std::uint8_t>((e.same_screen ? 1U : 0U) | (e.focus ? 2U : 0U)));
  assert(w.size() == 32); return std::move(w).take();
}
}  // namespace gw::protocol::x11
