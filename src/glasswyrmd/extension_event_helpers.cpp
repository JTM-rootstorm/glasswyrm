#include "glasswyrmd/extension_event_helpers.hpp"

#include "glasswyrmd/extension_registry.hpp"
#include "glasswyrmd/extension_wire.hpp"
#include "protocol/x11/byte_cursor.hpp"

namespace glasswyrm::server {
namespace x11 = gw::protocol::x11;

std::vector<std::uint8_t> encode_xfixes_selection_notify(
    const x11::ByteOrder order, const std::uint64_t sequence,
    const XFixesSelectionNotifyEvent& event) {
  x11::ByteWriter body(order);
  body.write_u32(event.window);
  body.write_u32(event.owner);
  body.write_u32(event.selection);
  body.write_u32(event.timestamp);
  body.write_u32(event.selection_timestamp);
  body.write_padding(8);
  const auto* extension = find_extension(ExtensionKind::XFixes);
  return extension
             ? encode_extension_event(order, *extension, 0, sequence,
                                      event.subtype, std::move(body).take())
                   .value_or(std::vector<std::uint8_t>{})
             : std::vector<std::uint8_t>{};
}

std::vector<std::uint8_t> encode_damage_notify(
    const x11::ByteOrder order, const std::uint64_t sequence,
    const DamageNotifyEvent& event) {
  x11::ByteWriter body(order);
  body.write_u32(event.drawable);
  body.write_u32(event.damage);
  body.write_u32(event.timestamp);
  body.write_u16(static_cast<std::uint16_t>(event.area.x));
  body.write_u16(static_cast<std::uint16_t>(event.area.y));
  body.write_u16(static_cast<std::uint16_t>(event.area.width));
  body.write_u16(static_cast<std::uint16_t>(event.area.height));
  body.write_u16(static_cast<std::uint16_t>(event.geometry.x));
  body.write_u16(static_cast<std::uint16_t>(event.geometry.y));
  body.write_u16(static_cast<std::uint16_t>(event.geometry.width));
  body.write_u16(static_cast<std::uint16_t>(event.geometry.height));
  const auto* extension = find_extension(ExtensionKind::Damage);
  return extension
             ? encode_extension_event(order, *extension, 0, sequence,
                                      event.level, std::move(body).take())
                   .value_or(std::vector<std::uint8_t>{})
             : std::vector<std::uint8_t>{};
}

}  // namespace glasswyrm::server
