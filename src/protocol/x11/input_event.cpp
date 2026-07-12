#include "protocol/x11/input_event.hpp"

#include "protocol/x11/byte_cursor.hpp"

#include <cassert>

namespace gw::protocol::x11 {

std::vector<std::uint8_t> encode_input_event(
    const ByteOrder order, const std::uint64_t sequence, const InputEvent& e) {
  assert(e.type == CoreEventType::KeyPress || e.type == CoreEventType::KeyRelease ||
         e.type == CoreEventType::ButtonPress || e.type == CoreEventType::ButtonRelease ||
         e.type == CoreEventType::MotionNotify);
  ByteWriter w(order);
  w.write_u8(static_cast<std::uint8_t>(e.type));
  w.write_u8(e.type == CoreEventType::MotionNotify ? 0 : e.detail);
  w.write_u16(wire_sequence(sequence));
  w.write_u32(e.time); w.write_u32(e.root); w.write_u32(e.event); w.write_u32(e.child);
  w.write_u16(static_cast<std::uint16_t>(e.root_x));
  w.write_u16(static_cast<std::uint16_t>(e.root_y));
  w.write_u16(static_cast<std::uint16_t>(e.event_x));
  w.write_u16(static_cast<std::uint16_t>(e.event_y));
  w.write_u16(e.state); w.write_u8(e.same_screen ? 1 : 0); w.write_u8(0);
  assert(w.size() == 32);
  return std::move(w).take();
}

}  // namespace gw::protocol::x11
