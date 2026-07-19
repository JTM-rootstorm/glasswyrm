#pragma once

#include "glasswyrmd/request_dispatcher.hpp"
#include "glasswyrmd/selection_store.hpp"

#include <vector>

namespace glasswyrm::server {

[[nodiscard]] std::vector<std::uint8_t> encode_xfixes_selection_notify(
    gw::protocol::x11::ByteOrder order, std::uint64_t sequence,
    const XFixesSelectionNotifyEvent& event);
[[nodiscard]] std::vector<std::uint8_t> encode_damage_notify(
    gw::protocol::x11::ByteOrder order, std::uint64_t sequence,
    const DamageNotifyEvent& event);
[[nodiscard]] std::vector<std::uint8_t> encode_randr_screen_change_notify(
    gw::protocol::x11::ByteOrder order, std::uint64_t sequence,
    const RandRScreenChangeNotifyEvent& event);
[[nodiscard]] std::vector<std::uint8_t> encode_randr_crtc_change_notify(
    gw::protocol::x11::ByteOrder order, std::uint64_t sequence,
    const RandRCrtcChangeNotifyEvent& event);
[[nodiscard]] std::vector<std::uint8_t> encode_randr_output_change_notify(
    gw::protocol::x11::ByteOrder order, std::uint64_t sequence,
    const RandROutputChangeNotifyEvent& event);
[[nodiscard]] std::vector<std::uint8_t> encode_randr_output_property_notify(
    gw::protocol::x11::ByteOrder order, std::uint64_t sequence,
    const RandROutputPropertyNotifyEvent& event);
[[nodiscard]] std::vector<std::uint8_t> encode_gw_scale_notify(
    gw::protocol::x11::ByteOrder order, std::uint64_t sequence,
    const GwScaleNotifyEvent& event);

void append_gw_scale_notifications(DispatchResult& result,
                                   const WindowResource& window,
                                   std::uint32_t xid,
                                   std::uint8_t reason_mask);

inline void append_xfixes_notifications(
    DispatchResult& result,
    const std::vector<XFixesSelectionNotification>& notifications) {
  for (const auto& notification : notifications) {
    ProtocolEventIntent intent;
    intent.delivery = ProtocolEventDelivery::DirectClient;
    intent.client = notification.client;
    intent.event = XFixesSelectionNotifyEvent{
        notification.subtype, notification.window, notification.owner,
        notification.selection, notification.timestamp,
        notification.selection_timestamp};
    result.protocol_events.push_back(std::move(intent));
  }
}

inline void append_damage_notifications(
    DispatchResult& result,
    const std::vector<DamageNotification>& notifications,
    const std::uint32_t timestamp) {
  for (const auto& notification : notifications) {
    ProtocolEventIntent intent;
    intent.delivery = ProtocolEventDelivery::DirectClient;
    intent.client = notification.client;
    intent.event = DamageNotifyEvent{
        static_cast<std::uint8_t>(notification.level), notification.drawable,
        notification.damage, timestamp, notification.area,
        notification.geometry};
    result.protocol_events.push_back(std::move(intent));
  }
}

}  // namespace glasswyrm::server
