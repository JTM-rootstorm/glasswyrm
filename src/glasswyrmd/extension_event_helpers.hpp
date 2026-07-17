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
