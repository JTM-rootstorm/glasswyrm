#include "glasswyrmd/ewmh_client_message.hpp"

#include "glasswyrmd/ewmh.hpp"
#include "glasswyrmd/request_handlers/common.hpp"

#include <algorithm>

namespace glasswyrm::server {
namespace x11 = gw::protocol::x11;

namespace {

std::uint32_t atom(const ServerState& state, const std::string_view name) {
  return state.atoms().find(name).value_or(0);
}

bool known_state(const ServerState& state, const std::uint32_t value) {
  for (const auto name : {"_NET_WM_STATE_FULLSCREEN",
                          "_NET_WM_STATE_MAXIMIZED_VERT",
                          "_NET_WM_STATE_MAXIMIZED_HORZ",
                          "_NET_WM_STATE_ABOVE"})
    if (value == atom(state, name)) return true;
  return false;
}

DispatchResult state_message(ServerState& state,
                             const DispatchContext& context,
                             const x11::FramedRequest& request,
                             const x11::ClientMessageEvent& event,
                             const std::array<std::uint32_t, 5>& data) {
  if (data[0] > 2)
    return request_handlers::error(context, request,
                                   x11::CoreErrorCode::BadValue, data[0]);
  if (!state.resources().is_policy_candidate(event.window))
    return request_handlers::error(context, request,
                                   x11::CoreErrorCode::BadWindow,
                                   event.window);
  auto staged = state;
  auto* window = staged.resources().find_window(event.window);
  const auto property_atom = atom(staged, "_NET_WM_STATE");
  std::vector<std::uint32_t> values;
  if (const auto found = window->properties.find(property_atom);
      found != window->properties.end())
    if (const auto* existing =
            std::get_if<std::vector<std::uint32_t>>(&found->second.data))
      values = *existing;
  for (const auto requested : {data[1], data[2]}) {
    if (requested == 0 || !known_state(staged, requested)) continue;
    const auto found = std::find(values.begin(), values.end(), requested);
    const bool present = found != values.end();
    const bool wanted = data[0] == 1 || (data[0] == 2 && !present);
    if (wanted && !present)
      values.push_back(requested);
    else if (!wanted && present)
      values.erase(found);
  }
  Property property{4, std::move(values)};
  if (staged.resources().change_property(event.window, property_atom, property,
                                         PropertyMode::Replace) !=
          PropertyMutationStatus::Success ||
      !interpret_ewmh_window(staged, event.window))
    return request_handlers::error(context, request,
                                   x11::CoreErrorCode::BadAlloc);
  const auto snapshot = staged.lifecycle_snapshot();
  const auto found = snapshot.windows.find(event.window);
  if (found == snapshot.windows.end())
    return request_handlers::error(context, request,
                                   x11::CoreErrorCode::BadImplementation);
  if (!context.integrated_lifecycle) {
    state = std::move(staged);
    synchronize_ewmh_root_properties(state);
    return {};
  }
  return DispatchResult::deferred_policy_change(
      {found->second,
       DeferredPropertyMutation{event.window, property_atom,
                                std::move(property),
                                context.input.logical_time},
       false});
}

DispatchResult active_message(ServerState& state,
                              const DispatchContext& context,
                              const x11::FramedRequest& request,
                              const x11::ClientMessageEvent& event) {
  if (!state.resources().is_policy_candidate(event.window))
    return request_handlers::error(context, request,
                                   x11::CoreErrorCode::BadWindow,
                                   event.window);
  if (!context.integrated_lifecycle) {
    auto snapshot = state.lifecycle_snapshot();
    for (auto& [xid, window] : snapshot.windows) window.focused = xid == event.window;
    snapshot.focused_window = event.window;
    (void)state.commit_lifecycle(snapshot);
    return {};
  }
  const auto snapshot = state.lifecycle_snapshot();
  const auto found = snapshot.windows.find(event.window);
  return DispatchResult::deferred_policy_change(
      {found->second, std::nullopt, true});
}

DispatchResult close_message(ServerState& state,
                             const DispatchContext& context,
                             const x11::FramedRequest& request,
                             const x11::ClientMessageEvent& event,
                             const std::array<std::uint32_t, 5>& data) {
  const auto* window = state.resources().find_window(event.window);
  if (!window)
    return request_handlers::error(context, request,
                                   x11::CoreErrorCode::BadWindow,
                                   event.window);
  const auto protocols = window->properties.find(atom(state, "WM_PROTOCOLS"));
  const auto* values = protocols == window->properties.end()
                           ? nullptr
                           : std::get_if<std::vector<std::uint32_t>>(
                                 &protocols->second.data);
  const auto delete_atom = atom(state, "WM_DELETE_WINDOW");
  if (!values || std::find(values->begin(), values->end(), delete_atom) ==
                     values->end())
    return request_handlers::error(context, request,
                                   x11::CoreErrorCode::BadAccess,
                                   event.window);
  x11::ClientMessageEvent notification;
  notification.window = event.window;
  notification.type = atom(state, "WM_PROTOCOLS");
  notification.data =
      std::array<std::uint32_t, 5>{delete_atom, data[0], 0, 0, 0};
  ProtocolEventIntent intent;
  intent.delivery = ProtocolEventDelivery::WindowOwner;
  intent.window = event.window;
  intent.event = std::move(notification);
  DispatchResult result;
  result.protocol_events.push_back(std::move(intent));
  return result;
}

}  // namespace

std::optional<DispatchResult> handle_ewmh_client_message(
    ServerState& state, const DispatchContext& context,
    const x11::FramedRequest& request, const std::uint32_t destination,
    const x11::ClientMessageEvent& event) {
  if (!state.game_compat() || destination != state.screen().root_window)
    return std::nullopt;
  const auto* data = std::get_if<std::array<std::uint32_t, 5>>(&event.data);
  if (!data) return std::nullopt;
  if (event.type == atom(state, "_NET_WM_STATE"))
    return state_message(state, context, request, event, *data);
  if (event.type == atom(state, "_NET_ACTIVE_WINDOW"))
    return active_message(state, context, request, event);
  if (event.type == atom(state, "_NET_CLOSE_WINDOW"))
    return close_message(state, context, request, event, *data);
  return std::nullopt;
}

}  // namespace glasswyrm::server
