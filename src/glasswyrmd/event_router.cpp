#include "glasswyrmd/event_router.hpp"

#include "protocol/x11/event.hpp"
#include "protocol/x11/exposure_event.hpp"
#include "input/input_router.hpp"
#include "protocol/x11/crossing_event.hpp"
#include "protocol/x11/focus_event.hpp"
#include "protocol/x11/input_event.hpp"
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

namespace {
std::vector<glasswyrm::input::RouteWindow> route_windows(
    const ResourceTable& resources, std::uint32_t source) {
  std::vector<glasswyrm::input::RouteWindow> result;
  while (source != 0) {
    const auto* window = resources.find_window(source);
    if (!window) break;
    glasswyrm::input::RouteWindow route{};
    route.xid = source;
    route.parent = window->parent;
    route.do_not_propagate = window->attributes.do_not_propagate_mask;
    route.selections.reserve(window->event_selections.size());
    for (const auto& [client, mask] : window->event_selections)
      route.selections.push_back({client, mask, true});
    result.push_back(std::move(route));
    if (window->parent == source) break;
    source = window->parent;
  }
  return result;
}
}  // namespace

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

std::size_t EventRouter::route_viewable_subtree_expose(
    const std::uint32_t window,
    const std::span<ClientConnection *const> clients) const {
  const auto* resource = resources_.find_window(window);
  if (!resource || resource->map_state != MapState::Viewable)
    return 0;
  std::size_t delivered = 0;
  if (resource->window_class == WindowClass::InputOutput) {
    const std::array rectangles{glasswyrm::geometry::Rectangle{
        0, 0, resource->width, resource->height}};
    delivered += route_expose(window, rectangles, clients);
  }
  for (const auto child : resource->children)
    delivered += route_viewable_subtree_expose(child, clients);
  return delivered;
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

std::size_t EventRouter::route_input(
    const gw::protocol::x11::CoreEventType type, const std::uint8_t detail,
    const std::uint32_t time, const std::uint32_t source,
    const std::uint16_t state, const std::uint32_t delivery_mask,
    const std::int32_t root_x, const std::int32_t root_y,
    const std::uint32_t pointer_target,
    const std::span<ClientConnection *const> clients) const {
  const auto windows = route_windows(resources_, source);
  const auto target = glasswyrm::input::propagate_event(
      windows, source, delivery_mask);
  if (target.event_window == 0) return 0;
  const auto coordinates = glasswyrm::input::event_coordinates(
      resources_, target.event_window, pointer_target, root_x, root_y);
  std::size_t delivered = 0;
  for (const auto client_id : target.clients) {
    auto* client = find_client(clients, client_id);
    if (!client) continue;
    gw::protocol::x11::InputEvent event{};
    event.type = type;
    event.detail = detail;
    event.time = time;
    event.root = resources_.screen().root_window;
    event.event = target.event_window;
    event.child = coordinates.child;
    event.root_x = coordinates.root_x;
    event.root_y = coordinates.root_y;
    event.event_x = coordinates.event_x;
    event.event_y = coordinates.event_y;
    event.state = state;
    if (client->enqueue_server_packet(gw::protocol::x11::encode_input_event(
            client->byte_order(), client->last_request_sequence(), event)))
      ++delivered;
  }
  return delivered;
}

std::size_t EventRouter::route_crossing(
    const std::uint32_t old_target, const std::uint32_t new_target,
    const std::uint32_t focus, const glasswyrm::input::InputState& input,
    const std::span<ClientConnection *const> clients) const {
  if (old_target == new_target) return 0;
  const auto root = resources_.screen().root_window;
  const auto details = glasswyrm::input::crossing_details(
      root, old_target, new_target);
  std::size_t delivered = 0;
  const auto deliver = [&](const std::uint32_t window,
                           const std::uint32_t mask,
                           const gw::protocol::x11::CoreEventType type,
                           const gw::protocol::x11::NotifyDetail detail) {
    const auto routes = route_windows(resources_, window);
    const auto target = glasswyrm::input::select_direct(routes, window, mask);
    const auto coordinates = glasswyrm::input::event_coordinates(
        resources_, window, input.pointer_target(), input.pointer_x(),
        input.pointer_y());
    for (const auto client_id : target.clients) {
      auto* client = find_client(clients, client_id);
      if (!client) continue;
      gw::protocol::x11::CrossingEvent event{};
      event.type = type;
      event.detail = detail;
      event.time = input.time();
      event.root = root;
      event.event = window;
      event.child = coordinates.child;
      event.root_x = coordinates.root_x;
      event.root_y = coordinates.root_y;
      event.event_x = coordinates.event_x;
      event.event_y = coordinates.event_y;
      event.state = input.mask();
      event.focus = glasswyrm::input::crossing_focus(root, window, focus);
      if (client->enqueue_server_packet(gw::protocol::x11::encode_crossing_event(
              client->byte_order(), client->last_request_sequence(), event)))
        ++delivered;
    }
  };
  deliver(old_target, gw::protocol::x11::event_mask::LeaveWindow,
          gw::protocol::x11::CoreEventType::LeaveNotify, details.first);
  deliver(new_target, gw::protocol::x11::event_mask::EnterWindow,
          gw::protocol::x11::CoreEventType::EnterNotify, details.second);
  return delivered;
}

std::size_t EventRouter::route_focus(
    const std::uint32_t old_focus, const std::uint32_t new_focus,
    const std::span<ClientConnection *const> clients) const {
  if (old_focus == new_focus) return 0;
  const auto root = resources_.screen().root_window;
  const auto details = glasswyrm::input::crossing_details(root, old_focus,
                                                           new_focus);
  std::size_t delivered = 0;
  const auto deliver = [&](const std::uint32_t window,
                           const gw::protocol::x11::CoreEventType type,
                           const gw::protocol::x11::NotifyDetail detail) {
    const auto routes = route_windows(resources_, window);
    const auto target = glasswyrm::input::select_direct(
        routes, window, gw::protocol::x11::event_mask::FocusChange);
    for (const auto client_id : target.clients) {
      auto* client = find_client(clients, client_id);
      if (!client) continue;
      const gw::protocol::x11::FocusEvent event{type, detail, window,
                                                gw::protocol::x11::NotifyMode::Normal};
      if (client->enqueue_server_packet(gw::protocol::x11::encode_focus_event(
              client->byte_order(), client->last_request_sequence(), event)))
        ++delivered;
    }
  };
  deliver(old_focus, gw::protocol::x11::CoreEventType::FocusOut, details.first);
  deliver(new_focus, gw::protocol::x11::CoreEventType::FocusIn, details.second);
  return delivered;
}

InputTransitionState EventRouter::capture_input_transition(
    const std::uint32_t focus, const std::uint32_t pointer_target) const {
  InputTransitionState result{};
  result.focus = focus;
  result.pointer_target = pointer_target;
  const auto capture = [&](const std::uint32_t xid, DirectInputEventState& out) {
    out.window = xid;
    const auto* window = resources_.find_window(xid);
    if (!window) return;
    out.x = window->x;
    out.y = window->y;
    for (const auto& [client, mask] : window->event_selections) {
      if ((mask & gw::protocol::x11::event_mask::FocusChange) != 0)
        out.focus_recipients.push_back(client);
      if ((mask & gw::protocol::x11::event_mask::LeaveWindow) != 0)
        out.leave_recipients.push_back(client);
    }
  };
  capture(focus, result.focus_window);
  capture(pointer_target, result.pointer_window);
  return result;
}

std::size_t EventRouter::route_lifecycle_input_transition(
    const InputTransitionState& before, const std::uint32_t new_focus,
    const std::uint32_t new_pointer_target,
    const glasswyrm::input::InputState& input,
    const std::span<ClientConnection *const> clients) const {
  std::size_t delivered = 0;
  const auto enqueue_focus = [&](const DirectInputEventState& state,
                                 const gw::protocol::x11::CoreEventType type,
                                 const gw::protocol::x11::NotifyDetail detail) {
    for (const auto id : state.focus_recipients) {
      auto* client = find_client(clients, id);
      if (!client) continue;
      const gw::protocol::x11::FocusEvent event{
          type, detail, state.window, gw::protocol::x11::NotifyMode::Normal};
      if (client->enqueue_server_packet(gw::protocol::x11::encode_focus_event(
              client->byte_order(), client->last_request_sequence(), event)))
        ++delivered;
    }
  };
  if (before.focus != new_focus) {
    const auto details = glasswyrm::input::crossing_details(
        resources_.screen().root_window, before.focus, new_focus);
    enqueue_focus(before.focus_window,
                  gw::protocol::x11::CoreEventType::FocusOut, details.first);
    const auto* arrival = resources_.find_window(new_focus);
    if (arrival) {
      for (const auto& [id, mask] : arrival->event_selections) {
        if ((mask & gw::protocol::x11::event_mask::FocusChange) == 0) continue;
        auto* client = find_client(clients, id);
        if (!client) continue;
        const gw::protocol::x11::FocusEvent event{
            gw::protocol::x11::CoreEventType::FocusIn, details.second,
            new_focus, gw::protocol::x11::NotifyMode::Normal};
        if (client->enqueue_server_packet(gw::protocol::x11::encode_focus_event(
                client->byte_order(), client->last_request_sequence(), event)))
          ++delivered;
      }
    }
  }

  if (before.pointer_target != new_pointer_target) {
    const auto root = resources_.screen().root_window;
    const auto details = glasswyrm::input::crossing_details(
        root, before.pointer_target, new_pointer_target);
    for (const auto id : before.pointer_window.leave_recipients) {
      auto* client = find_client(clients, id);
      if (!client) continue;
      gw::protocol::x11::CrossingEvent event{};
      event.type = gw::protocol::x11::CoreEventType::LeaveNotify;
      event.detail = details.first;
      event.time = input.time();
      event.root = root;
      event.event = before.pointer_target;
      event.child = before.pointer_target == root ? new_pointer_target : 0;
      event.root_x = input.pointer_x();
      event.root_y = input.pointer_y();
      event.event_x = before.pointer_target == root
                          ? input.pointer_x()
                          : input.pointer_x() - before.pointer_window.x;
      event.event_y = before.pointer_target == root
                          ? input.pointer_y()
                          : input.pointer_y() - before.pointer_window.y;
      event.state = input.mask();
      event.focus = glasswyrm::input::crossing_focus(
          root, before.pointer_target, new_focus);
      if (client->enqueue_server_packet(gw::protocol::x11::encode_crossing_event(
              client->byte_order(), client->last_request_sequence(), event)))
        ++delivered;
    }
    // The old target is intentionally equal to the new target here only for
    // selecting the committed EnterNotify recipients; emit it explicitly.
    const auto* window = resources_.find_window(new_pointer_target);
    if (window) {
      const auto coordinates = glasswyrm::input::event_coordinates(
          resources_, new_pointer_target, new_pointer_target,
          input.pointer_x(), input.pointer_y());
      for (const auto& [id, mask] : window->event_selections) {
        if ((mask & gw::protocol::x11::event_mask::EnterWindow) == 0) continue;
        auto* client = find_client(clients, id);
        if (!client) continue;
        gw::protocol::x11::CrossingEvent event{};
        event.type = gw::protocol::x11::CoreEventType::EnterNotify;
        event.detail = details.second;
        event.time = input.time();
        event.root = root;
        event.event = new_pointer_target;
        event.child = coordinates.child;
        event.root_x = coordinates.root_x;
        event.root_y = coordinates.root_y;
        event.event_x = coordinates.event_x;
        event.event_y = coordinates.event_y;
        event.state = input.mask();
        event.focus = glasswyrm::input::crossing_focus(root, new_pointer_target,
                                                       new_focus);
        if (client->enqueue_server_packet(
                gw::protocol::x11::encode_crossing_event(
                    client->byte_order(), client->last_request_sequence(), event)))
          ++delivered;
      }
    }
  }
  return delivered;
}

} // namespace glasswyrm::server
