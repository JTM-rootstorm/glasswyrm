#include "glasswyrmd/extension_wire.hpp"

#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/core.hpp"
#include "protocol/x11/reply.hpp"

namespace glasswyrm::server {
namespace x11 = gw::protocol::x11;

std::optional<std::vector<std::uint8_t>> encode_extension_error(
    const x11::ByteOrder order, const ExtensionDescriptor& extension,
    const std::uint8_t relative_error, const std::uint64_t sequence,
    const std::uint32_t bad_value, const std::uint8_t major_opcode,
    const std::uint8_t minor_opcode) {
  if (relative_error >= extension.error_count) return std::nullopt;
  const auto absolute =
      static_cast<std::uint8_t>(extension.first_error + relative_error);
  return x11::encode_core_error(
      order, {static_cast<x11::CoreErrorCode>(absolute), sequence, bad_value,
              major_opcode, minor_opcode});
}

std::optional<std::vector<std::uint8_t>> encode_extension_event(
    const x11::ByteOrder order, const ExtensionDescriptor& extension,
    const std::uint8_t relative_event, const std::uint64_t sequence,
    const std::uint8_t detail, const std::span<const std::uint8_t> event_body,
    const bool send_event) {
  if (relative_event >= extension.event_count || event_body.size() > 28)
    return std::nullopt;
  x11::ByteWriter writer(order);
  auto event_type =
      static_cast<std::uint8_t>(extension.first_event + relative_event);
  if (send_event) event_type |= 0x80U;
  writer.write_u8(event_type);
  writer.write_u8(detail);
  writer.write_u16(x11::wire_sequence(sequence));
  writer.write_bytes(event_body);
  writer.write_padding(28 - event_body.size());
  return std::move(writer).take();
}

}  // namespace glasswyrm::server
