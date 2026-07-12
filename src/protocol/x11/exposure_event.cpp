#include "protocol/x11/exposure_event.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/core.hpp"
#include <cassert>

namespace gw::protocol::x11 {
namespace {
ByteWriter header(ByteOrder order, CoreEventType type, std::uint64_t sequence) {
  ByteWriter writer(order); writer.write_u8(static_cast<std::uint8_t>(type));
  writer.write_u8(0); writer.write_u16(wire_sequence(sequence)); return writer;
}
std::vector<std::uint8_t> finish(ByteWriter writer) {
  assert(writer.size() <= 32); writer.write_padding(32 - writer.size());
  return std::move(writer).take();
}
}
std::vector<std::uint8_t> encode_expose(ByteOrder order, std::uint64_t sequence,
                                        const ExposeEvent& event) {
  auto writer = header(order, CoreEventType::Expose, sequence);
  writer.write_u32(event.window); writer.write_u16(event.x); writer.write_u16(event.y);
  writer.write_u16(event.width); writer.write_u16(event.height); writer.write_u16(event.count);
  return finish(std::move(writer));
}
std::vector<std::uint8_t> encode_graphics_expose(ByteOrder order, std::uint64_t sequence,
                                                 const GraphicsExposeEvent& event) {
  auto writer = header(order, CoreEventType::GraphicsExpose, sequence);
  writer.write_u32(event.drawable); writer.write_u16(event.x); writer.write_u16(event.y);
  writer.write_u16(event.width); writer.write_u16(event.height); writer.write_u16(event.minor_opcode);
  writer.write_u16(event.count); writer.write_u8(event.major_opcode); writer.write_padding(3);
  return finish(std::move(writer));
}
std::vector<std::uint8_t> encode_no_expose(ByteOrder order, std::uint64_t sequence,
                                           const NoExposeEvent& event) {
  auto writer = header(order, CoreEventType::NoExpose, sequence);
  writer.write_u32(event.drawable); writer.write_u16(event.minor_opcode);
  writer.write_u8(event.major_opcode); writer.write_padding(1);
  return finish(std::move(writer));
}
}  // namespace gw::protocol::x11
