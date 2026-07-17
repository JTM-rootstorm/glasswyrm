#include "protocol/x11/event.hpp"

#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/core.hpp"

#include <cassert>
#include <type_traits>

namespace gw::protocol::x11 {
namespace {

ByteWriter event_header(const ByteOrder order, const CoreEventType type,
                        const std::uint64_t sequence,
                        const bool synthetic = false) {
  ByteWriter writer(order);
  writer.write_u8(static_cast<std::uint8_t>(type) |
                  (synthetic ? UINT8_C(0x80) : UINT8_C(0)));
  writer.write_u8(0);
  writer.write_u16(wire_sequence(sequence));
  return writer;
}

std::vector<std::uint8_t> finish_event(ByteWriter writer) {
  assert(writer.size() <= 32);
  writer.write_padding(32 - writer.size());
  return std::move(writer).take();
}

}  // namespace

std::vector<std::uint8_t> encode_destroy_notify(
    const ByteOrder order, const std::uint64_t sequence,
    const DestroyNotifyEvent& event) {
  auto writer = event_header(order, CoreEventType::DestroyNotify, sequence);
  writer.write_u32(event.event);
  writer.write_u32(event.window);
  return finish_event(std::move(writer));
}

std::vector<std::uint8_t> encode_unmap_notify(
    const ByteOrder order, const std::uint64_t sequence,
    const UnmapNotifyEvent& event) {
  auto writer = event_header(order, CoreEventType::UnmapNotify, sequence,
                             event.synthetic);
  writer.write_u32(event.event);
  writer.write_u32(event.window);
  writer.write_u8(event.from_configure ? 1 : 0);
  writer.write_padding(3);
  return finish_event(std::move(writer));
}

std::vector<std::uint8_t> encode_map_notify(
    const ByteOrder order, const std::uint64_t sequence,
    const MapNotifyEvent& event) {
  auto writer = event_header(order, CoreEventType::MapNotify, sequence);
  writer.write_u32(event.event);
  writer.write_u32(event.window);
  writer.write_u8(event.override_redirect ? 1 : 0);
  writer.write_padding(3);
  return finish_event(std::move(writer));
}

std::vector<std::uint8_t> encode_configure_notify(
    const ByteOrder order, const std::uint64_t sequence,
    const ConfigureNotifyEvent& event) {
  auto writer = event_header(order, CoreEventType::ConfigureNotify, sequence);
  writer.write_u32(event.event);
  writer.write_u32(event.window);
  writer.write_u32(event.above_sibling);
  writer.write_u16(static_cast<std::uint16_t>(event.x));
  writer.write_u16(static_cast<std::uint16_t>(event.y));
  writer.write_u16(event.width);
  writer.write_u16(event.height);
  writer.write_u16(event.border_width);
  writer.write_u8(event.override_redirect ? 1 : 0);
  writer.write_u8(0);
  return finish_event(std::move(writer));
}

std::vector<std::uint8_t> encode_property_notify(
    const ByteOrder order, const std::uint64_t sequence,
    const PropertyNotifyEvent& event) {
  auto writer = event_header(order, CoreEventType::PropertyNotify, sequence);
  writer.write_u32(event.window);
  writer.write_u32(event.atom);
  writer.write_u32(event.time);
  writer.write_u8(static_cast<std::uint8_t>(event.state));
  return finish_event(std::move(writer));
}

std::vector<std::uint8_t> encode_selection_clear(
    const ByteOrder order, const std::uint64_t sequence,
    const SelectionClearEvent& event) {
  auto writer = event_header(order, CoreEventType::SelectionClear, sequence);
  writer.write_u32(event.time);
  writer.write_u32(event.owner);
  writer.write_u32(event.selection);
  return finish_event(std::move(writer));
}

std::vector<std::uint8_t> encode_selection_request(
    const ByteOrder order, const std::uint64_t sequence,
    const SelectionRequestEvent& event) {
  auto writer = event_header(order, CoreEventType::SelectionRequest, sequence);
  writer.write_u32(event.time);
  writer.write_u32(event.owner);
  writer.write_u32(event.requestor);
  writer.write_u32(event.selection);
  writer.write_u32(event.target);
  writer.write_u32(event.property);
  return finish_event(std::move(writer));
}

std::vector<std::uint8_t> encode_selection_notify(
    const ByteOrder order, const std::uint64_t sequence,
    const SelectionNotifyEvent& event) {
  auto writer = event_header(order, CoreEventType::SelectionNotify, sequence,
                             event.synthetic);
  writer.write_u32(event.time);
  writer.write_u32(event.requestor);
  writer.write_u32(event.selection);
  writer.write_u32(event.target);
  writer.write_u32(event.property);
  return finish_event(std::move(writer));
}

std::vector<std::uint8_t> encode_client_message(
    const ByteOrder order, const std::uint64_t sequence,
    const ClientMessageEvent& event) {
  ByteWriter writer(order);
  writer.write_u8(static_cast<std::uint8_t>(CoreEventType::ClientMessage) |
                  (event.synthetic ? UINT8_C(0x80) : UINT8_C(0)));
  writer.write_u8(static_cast<std::uint8_t>(std::visit([](const auto& data) {
    using Value = typename std::decay_t<decltype(data)>::value_type;
    return sizeof(Value) * 8U;
  }, event.data)));
  writer.write_u16(wire_sequence(sequence));
  writer.write_u32(event.window);
  writer.write_u32(event.type);
  std::visit([&writer](const auto& data) {
    using Value = typename std::decay_t<decltype(data)>::value_type;
    for (const auto value : data) {
      if constexpr (sizeof(Value) == 1)
        writer.write_u8(value);
      else if constexpr (sizeof(Value) == 2)
        writer.write_u16(value);
      else
        writer.write_u32(value);
    }
  }, event.data);
  return finish_event(std::move(writer));
}

}  // namespace gw::protocol::x11
