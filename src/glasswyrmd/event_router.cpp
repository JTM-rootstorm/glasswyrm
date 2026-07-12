#include "glasswyrmd/event_router.hpp"

#include "protocol/x11/event.hpp"

#include <algorithm>

namespace glasswyrm::server {
namespace {
constexpr std::uint32_t kStructureNotifyMask = 1U << 17U;
constexpr std::uint32_t kSubstructureNotifyMask = 1U << 19U;

ClientConnection* find_client(std::span<ClientConnection* const> clients,
                              const ClientId id) {
  const auto found = std::ranges::find(clients, id,
                                       &ClientConnection::identifier);
  return found == clients.end() ? nullptr : *found;
}

template <class Encoder, class EventFactory>
std::size_t route(const ResourceTable& resources, const std::uint32_t target,
                  const std::uint32_t parent,
                  std::span<ClientConnection* const> clients, Encoder encoder,
                  EventFactory event_factory) {
  std::size_t delivered = 0;
  const auto deliver = [&](const std::uint32_t selected_window,
                           const std::uint32_t required_mask,
                           const std::uint32_t event_window) {
    const auto* selected = resources.find_window(selected_window);
    if (!selected) return;
    for (const auto& [client_id, mask] : selected->event_selections) {
      if ((mask & required_mask) == 0) continue;
      auto* client = find_client(clients, client_id);
      if (!client) continue;
      auto bytes = encoder(client->byte_order(), client->last_request_sequence(),
                           event_factory(event_window));
      if (client->enqueue_server_packet(std::move(bytes))) ++delivered;
    }
  };
  deliver(target, kStructureNotifyMask, target);
  deliver(parent, kSubstructureNotifyMask, parent);
  return delivered;
}
}  // namespace

std::size_t EventRouter::route_destroy(
    const std::uint32_t target, const std::uint32_t parent,
    const std::span<ClientConnection* const> clients) const {
  return route(resources_, target, parent, clients,
               gw::protocol::x11::encode_destroy_notify,
               [target](const std::uint32_t event) {
                 return gw::protocol::x11::DestroyNotifyEvent{event, target};
               });
}

std::size_t EventRouter::route_unmap(
    const std::uint32_t target, const std::uint32_t parent,
    const std::span<ClientConnection* const> clients) const {
  return route(resources_, target, parent, clients,
               gw::protocol::x11::encode_unmap_notify,
               [target](const std::uint32_t event) {
                 return gw::protocol::x11::UnmapNotifyEvent{event, target,
                                                            false};
               });
}

std::size_t EventRouter::route_map(
    const std::uint32_t target, const std::uint32_t parent,
    const bool override_redirect,
    const std::span<ClientConnection* const> clients) const {
  return route(resources_, target, parent, clients,
               gw::protocol::x11::encode_map_notify,
               [target, override_redirect](const std::uint32_t event) {
                 return gw::protocol::x11::MapNotifyEvent{
                     event, target, override_redirect};
               });
}

std::size_t EventRouter::route_configure(
    const std::uint32_t target, const std::uint32_t parent,
    const std::uint32_t above_sibling, const std::int16_t x,
    const std::int16_t y, const std::uint16_t width,
    const std::uint16_t height, const std::uint16_t border_width,
    const bool override_redirect,
    const std::span<ClientConnection* const> clients) const {
  return route(resources_, target, parent, clients,
               gw::protocol::x11::encode_configure_notify,
               [&](const std::uint32_t event) {
                 return gw::protocol::x11::ConfigureNotifyEvent{
                     event, target, above_sibling, x, y, width, height,
                     border_width, override_redirect};
               });
}

}  // namespace glasswyrm::server
