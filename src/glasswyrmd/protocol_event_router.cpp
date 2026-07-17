#include "glasswyrmd/protocol_event_router.hpp"

#include "glasswyrmd/extension_event_helpers.hpp"
#include "protocol/x11/event.hpp"

#include <type_traits>

namespace glasswyrm::server {
namespace x11 = gw::protocol::x11;

namespace {

ClientConnection* find_client(std::span<ClientConnection* const> clients,
                              const ClientId id) {
  for (auto* client : clients)
    if (client && client->identifier() == id) return client;
  return nullptr;
}

std::vector<std::uint8_t> encode_for(const ClientConnection& client,
                                     const ProtocolEvent& event) {
  return std::visit(
      [&client](const auto& value) {
        using Event = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Event, x11::PropertyNotifyEvent>)
          return x11::encode_property_notify(
              client.byte_order(), client.last_request_sequence(), value);
        else if constexpr (std::is_same_v<Event, x11::SelectionClearEvent>)
          return x11::encode_selection_clear(
              client.byte_order(), client.last_request_sequence(), value);
        else if constexpr (std::is_same_v<Event, x11::SelectionRequestEvent>)
          return x11::encode_selection_request(
              client.byte_order(), client.last_request_sequence(), value);
        else if constexpr (std::is_same_v<Event, x11::SelectionNotifyEvent>)
          return x11::encode_selection_notify(
              client.byte_order(), client.last_request_sequence(), value);
        else if constexpr (std::is_same_v<Event, x11::UnmapNotifyEvent>)
          return x11::encode_unmap_notify(
              client.byte_order(), client.last_request_sequence(), value);
        else if constexpr (std::is_same_v<Event, x11::ClientMessageEvent>)
          return x11::encode_client_message(
              client.byte_order(), client.last_request_sequence(), value);
        else if constexpr (std::is_same_v<Event,
                                          XFixesSelectionNotifyEvent>) {
          return encode_xfixes_selection_notify(
              client.byte_order(), client.last_request_sequence(), value);
        } else if constexpr (std::is_same_v<Event, DamageNotifyEvent>) {
          return encode_damage_notify(client.byte_order(),
                                      client.last_request_sequence(), value);
        } else if constexpr (std::is_same_v<
                                 Event, RandRScreenChangeNotifyEvent>) {
          return encode_randr_screen_change_notify(
              client.byte_order(), client.last_request_sequence(), value);
        } else if constexpr (std::is_same_v<Event,
                                            RandRCrtcChangeNotifyEvent>) {
          return encode_randr_crtc_change_notify(
              client.byte_order(), client.last_request_sequence(), value);
        } else if constexpr (std::is_same_v<Event,
                                            RandROutputChangeNotifyEvent>) {
          return encode_randr_output_change_notify(
              client.byte_order(), client.last_request_sequence(), value);
        } else {
          return encode_randr_output_property_notify(
              client.byte_order(), client.last_request_sequence(), value);
        }
      },
      event);
}

bool deliver(ClientConnection* client, const ProtocolEvent& event) {
  return client && client->enqueue_server_packet(encode_for(*client, event));
}

}  // namespace

std::size_t ProtocolEventRouter::route(
    const ProtocolEventIntent& intent,
    const std::span<ClientConnection* const> clients) const {
  if (intent.delivery == ProtocolEventDelivery::DirectClient)
    return deliver(find_client(clients, intent.client), intent.event) ? 1 : 0;

  if (intent.delivery == ProtocolEventDelivery::WindowOwner) {
    const auto* record = resources_.find(intent.window);
    return record && record->owner &&
                   deliver(find_client(clients, *record->owner), intent.event)
               ? 1
               : 0;
  }

  std::uint32_t current = intent.window;
  while (const auto* window = resources_.find_window(current)) {
    std::size_t delivered = 0;
    for (const auto& [client_id, mask] : window->event_selections)
      if ((mask & intent.mask) != 0 &&
          deliver(find_client(clients, client_id), intent.event))
        ++delivered;
    if (delivered != 0 || !intent.propagate) return delivered;
    if ((window->attributes.do_not_propagate_mask & intent.mask) != 0)
      return 0;
    if (window->parent == 0 || window->parent == current) return 0;
    current = window->parent;
  }
  return 0;
}

}  // namespace glasswyrm::server
