#include "glasswyrmd/event_router.hpp"

#include "protocol/x11/event.hpp"
#include "protocol/x11/exposure_event.hpp"
#include "protocol/x11/event_mask.hpp"

#include <algorithm>

namespace glasswyrm::server {
namespace {
constexpr auto kStructureNotifyMask = gw::protocol::x11::event_mask::StructureNotify;
constexpr auto kSubstructureNotifyMask = gw::protocol::x11::event_mask::SubstructureNotify;
constexpr auto kExposureMask = gw::protocol::x11::event_mask::Exposure;

ClientConnection *find_client(std::span<ClientConnection *const> clients,
                              const ClientId id) {
  const auto found =
      std::ranges::find(clients, id, &ClientConnection::identifier);
  return found == clients.end() ? nullptr : *found;
}

template <class Encoder, class EventFactory>
std::size_t route(const ResourceTable &resources, const std::uint32_t target,
                  const std::uint32_t parent,
                  std::span<ClientConnection *const> clients, Encoder encoder,
                  EventFactory event_factory) {
  std::size_t delivered = 0;
  const auto deliver = [&](const std::uint32_t selected_window,
                           const std::uint32_t required_mask,
                           const std::uint32_t event_window) {
    const auto *selected = resources.find_window(selected_window);
    if (!selected)
      return;
    for (const auto &[client_id, mask] : selected->event_selections) {
      if ((mask & required_mask) == 0)
        continue;
      auto *client = find_client(clients, client_id);
      if (!client)
        continue;
      auto bytes =
          encoder(client->byte_order(), client->last_request_sequence(),
                  event_factory(event_window));
      if (client->enqueue_server_packet(std::move(bytes)))
        ++delivered;
    }
  };
  deliver(target, kStructureNotifyMask, target);
  deliver(parent, kSubstructureNotifyMask, parent);
  return delivered;
}
} // namespace

std::optional<StructuralEventState>
EventRouter::capture(const std::uint32_t target) const {
  const auto *window = resources_.find_window(target);
  if (!window)
    return std::nullopt;
  const auto *parent = resources_.find_window(window->parent);
  StructuralEventState result{};
  result.target = target;
  result.parent = window->parent;
  result.x = window->x;
  result.y = window->y;
  result.width = window->width;
  result.height = window->height;
  result.border_width = window->border_width;
  result.override_redirect = window->attributes.override_redirect;
  result.mapped = window->map_requested;
  result.viewable = window->map_state == MapState::Viewable;
  if (parent) {
    const auto position = std::ranges::find(parent->children, target);
    if (position != parent->children.end() &&
        std::next(position) != parent->children.end())
      result.above_sibling = *std::next(position);
  }
  for (const auto &[client, mask] : window->event_selections)
    if ((mask & kStructureNotifyMask) != 0)
      result.structure_recipients.push_back(client);
  if (parent)
    for (const auto &[client, mask] : parent->event_selections)
      if ((mask & kSubstructureNotifyMask) != 0)
        result.substructure_recipients.push_back(client);
  return result;
}

std::size_t EventRouter::route_transition(
    const StructuralTransitionKind kind,
    const std::optional<StructuralEventState> &before,
    const std::optional<StructuralEventState> &committed,
    const std::span<ClientConnection *const> clients) const {
  const auto *state = committed ? &*committed : before ? &*before : nullptr;
  if (!state)
    return 0;
  bool changed = false;
  switch (kind) {
  case StructuralTransitionKind::Map:
    changed = before && committed && !before->mapped && committed->mapped;
    break;
  case StructuralTransitionKind::Unmap:
    changed = before && committed && before->mapped && !committed->mapped;
    break;
  case StructuralTransitionKind::Configure:
    changed = before && committed &&
              (before->x != committed->x || before->y != committed->y ||
               before->width != committed->width ||
               before->height != committed->height ||
               before->border_width != committed->border_width ||
               before->above_sibling != committed->above_sibling);
    break;
  case StructuralTransitionKind::Destroy:
    changed = before && !committed;
    break;
  }
  if (!changed)
    return 0;
  std::size_t delivered = 0;
  const auto deliver = [&](const std::vector<ClientId> &recipients,
                           const std::uint32_t event_window, auto encoder,
                           auto event) {
    for (const auto client_id : recipients) {
      auto *client = find_client(clients, client_id);
      if (!client)
        continue;
      auto value = event;
      value.event = event_window;
      if (client->enqueue_server_packet(encoder(
              client->byte_order(), client->last_request_sequence(), value)))
        ++delivered;
    }
  };
  const auto route_both = [&](auto encoder, auto event) {
    deliver(state->structure_recipients, state->target, encoder, event);
    deliver(state->substructure_recipients, state->parent, encoder, event);
  };
  if (kind == StructuralTransitionKind::Map)
    route_both(gw::protocol::x11::encode_map_notify,
               gw::protocol::x11::MapNotifyEvent{0, state->target,
                                                 state->override_redirect});
  else if (kind == StructuralTransitionKind::Unmap)
    route_both(gw::protocol::x11::encode_unmap_notify,
               gw::protocol::x11::UnmapNotifyEvent{0, state->target, false});
  else if (kind == StructuralTransitionKind::Configure)
    route_both(gw::protocol::x11::encode_configure_notify,
               gw::protocol::x11::ConfigureNotifyEvent{
                   0, state->target, state->above_sibling, state->x, state->y,
                   state->width, state->height, state->border_width,
                   state->override_redirect});
  else
    route_both(gw::protocol::x11::encode_destroy_notify,
               gw::protocol::x11::DestroyNotifyEvent{0, state->target});
  return delivered;
}

std::size_t EventRouter::route_destroy(
    const std::uint32_t target, const std::uint32_t parent,
    const std::span<ClientConnection *const> clients) const {
  return route(resources_, target, parent, clients,
               gw::protocol::x11::encode_destroy_notify,
               [target](const std::uint32_t event) {
                 return gw::protocol::x11::DestroyNotifyEvent{event, target};
               });
}

std::size_t EventRouter::route_unmap(
    const std::uint32_t target, const std::uint32_t parent,
    const std::span<ClientConnection *const> clients) const {
  return route(resources_, target, parent, clients,
               gw::protocol::x11::encode_unmap_notify,
               [target](const std::uint32_t event) {
                 return gw::protocol::x11::UnmapNotifyEvent{event, target,
                                                            false};
               });
}

std::size_t
EventRouter::route_map(const std::uint32_t target, const std::uint32_t parent,
                       const bool override_redirect,
                       const std::span<ClientConnection *const> clients) const {
  return route(resources_, target, parent, clients,
               gw::protocol::x11::encode_map_notify,
               [target, override_redirect](const std::uint32_t event) {
                 return gw::protocol::x11::MapNotifyEvent{event, target,
                                                          override_redirect};
               });
}

std::size_t EventRouter::route_configure(
    const std::uint32_t target, const std::uint32_t parent, const std::uint32_t,
    const std::int16_t x, const std::int16_t y, const std::uint16_t width,
    const std::uint16_t height, const std::uint16_t border_width,
    const bool override_redirect,
    const std::span<ClientConnection *const> clients) const {
  const auto committed = capture(target);
  const auto above_sibling = committed ? committed->above_sibling : 0;
  return route(resources_, target, parent, clients,
               gw::protocol::x11::encode_configure_notify,
               [&](const std::uint32_t event) {
                 return gw::protocol::x11::ConfigureNotifyEvent{
                     event,  target,       above_sibling,    x, y, width,
                     height, border_width, override_redirect};
               });
}

std::size_t EventRouter::route_expose(
    const std::uint32_t window_id,
    const std::span<const glasswyrm::geometry::Rectangle> rectangles,
    const std::span<ClientConnection *const> clients) const {
  const auto* window = resources_.find_window(window_id);
  if (!window) return 0;
  std::size_t delivered = 0;
  for (const auto& [client_id, mask] : window->event_selections) {
    if ((mask & kExposureMask) == 0) continue;
    auto* client = find_client(clients, client_id);
    if (!client) continue;
    for (std::size_t index = 0; index < rectangles.size(); ++index) {
      const auto& rectangle = rectangles[index];
      if (client->enqueue_server_packet(gw::protocol::x11::encode_expose(
          client->byte_order(), client->last_request_sequence(),
          {window_id, static_cast<std::uint16_t>(rectangle.x),
           static_cast<std::uint16_t>(rectangle.y),
           static_cast<std::uint16_t>(rectangle.width),
           static_cast<std::uint16_t>(rectangle.height),
           static_cast<std::uint16_t>(rectangles.size() - index - 1)})))
        ++delivered;
    }
  }
  return delivered;
}

} // namespace glasswyrm::server
