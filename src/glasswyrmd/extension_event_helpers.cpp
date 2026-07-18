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

std::vector<std::uint8_t> encode_randr_screen_change_notify(
    const x11::ByteOrder order, const std::uint64_t sequence,
    const RandRScreenChangeNotifyEvent& event) {
  x11::ByteWriter body(order);
  body.write_u32(event.timestamp);
  body.write_u32(event.config_timestamp);
  body.write_u32(event.root);
  body.write_u32(event.request_window);
  body.write_u16(event.size_id);
  body.write_u16(event.subpixel_order);
  body.write_u16(event.width);
  body.write_u16(event.height);
  body.write_u16(event.width_millimeters);
  body.write_u16(event.height_millimeters);
  const auto* extension = find_extension(ExtensionKind::RandR);
  return extension
             ? encode_extension_event(
                   order, *extension, 0, sequence,
                   static_cast<std::uint8_t>(event.rotation),
                   std::move(body).take())
                   .value_or(std::vector<std::uint8_t>{})
             : std::vector<std::uint8_t>{};
}

std::vector<std::uint8_t> encode_randr_crtc_change_notify(
    const x11::ByteOrder order, const std::uint64_t sequence,
    const RandRCrtcChangeNotifyEvent& event) {
  x11::ByteWriter body(order);
  body.write_u32(event.timestamp);
  body.write_u32(event.window);
  body.write_u32(event.crtc);
  body.write_u32(event.mode);
  body.write_u16(event.rotation);
  body.write_padding(2);
  body.write_u16(static_cast<std::uint16_t>(event.x));
  body.write_u16(static_cast<std::uint16_t>(event.y));
  body.write_u16(event.width);
  body.write_u16(event.height);
  const auto* extension = find_extension(ExtensionKind::RandR);
  return extension
             ? encode_extension_event(order, *extension, 1, sequence, 0,
                                      std::move(body).take())
                   .value_or(std::vector<std::uint8_t>{})
             : std::vector<std::uint8_t>{};
}

std::vector<std::uint8_t> encode_randr_output_change_notify(
    const x11::ByteOrder order, const std::uint64_t sequence,
    const RandROutputChangeNotifyEvent& event) {
  x11::ByteWriter body(order);
  body.write_u32(event.timestamp);
  body.write_u32(event.config_timestamp);
  body.write_u32(event.window);
  body.write_u32(event.output);
  body.write_u32(event.crtc);
  body.write_u32(event.mode);
  body.write_u16(event.rotation);
  body.write_u8(event.connection);
  body.write_u8(event.subpixel_order);
  const auto* extension = find_extension(ExtensionKind::RandR);
  return extension
             ? encode_extension_event(order, *extension, 1, sequence, 1,
                                      std::move(body).take())
                   .value_or(std::vector<std::uint8_t>{})
             : std::vector<std::uint8_t>{};
}

std::vector<std::uint8_t> encode_randr_output_property_notify(
    const x11::ByteOrder order, const std::uint64_t sequence,
    const RandROutputPropertyNotifyEvent& event) {
  x11::ByteWriter body(order);
  body.write_u32(event.window);
  body.write_u32(event.output);
  body.write_u32(event.atom);
  body.write_u32(event.timestamp);
  body.write_u8(event.status);
  body.write_padding(11);
  const auto* extension = find_extension(ExtensionKind::RandR);
  return extension
             ? encode_extension_event(order, *extension, 1, sequence, 2,
                                      std::move(body).take())
                   .value_or(std::vector<std::uint8_t>{})
             : std::vector<std::uint8_t>{};
}

}  // namespace glasswyrm::server
