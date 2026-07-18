#include "glasswyrmd/request_handlers/common.hpp"
#include "glasswyrmd/ewmh_client_message.hpp"
#include "glasswyrmd/extension_event_helpers.hpp"

#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/event_mask.hpp"
#include "protocol/x11/reply.hpp"

#include <array>

namespace glasswyrm::server::request_handlers {
namespace x11 = gw::protocol::x11;

namespace {

ProtocolEventIntent direct_event(const ClientId client, ProtocolEvent event) {
  ProtocolEventIntent intent;
  intent.delivery = ProtocolEventDelivery::DirectClient;
  intent.client = client;
  intent.event = std::move(event);
  return intent;
}

ProtocolEventIntent window_event(const std::uint32_t window,
                                 const std::uint32_t mask,
                                 const bool propagate,
                                 ProtocolEvent event) {
  ProtocolEventIntent intent;
  intent.delivery = mask == 0 ? ProtocolEventDelivery::WindowOwner
                              : ProtocolEventDelivery::WindowMask;
  intent.window = window;
  intent.mask = mask;
  intent.propagate = propagate;
  intent.event = std::move(event);
  return intent;
}

}  // namespace

DispatchResult set_selection_owner(ServerState& state,
                                   const DispatchContext& context,
                                   const x11::FramedRequest& request) {
  if (!exact_size(request, 16))
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t owner = 0, selection = 0, time = 0;
  if (!reader.read_u32(owner) || !reader.read_u32(selection) ||
      !reader.read_u32(time))
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (!state.atoms().valid(selection))
    return error(context, request, x11::CoreErrorCode::BadAtom, selection);
  const bool owner_exists = owner == 0 || state.resources().find_window(owner);
  if (!owner_exists)
    return error(context, request, x11::CoreErrorCode::BadWindow, owner);

  const auto change = state.selections().set_owner(
      context.client_id, selection, owner, owner_exists, time,
      context.input.logical_time);
  if (change.status == SelectionOwnershipStatus::IgnoredStaleTime) return {};
  if (change.status == SelectionOwnershipStatus::InvalidOwnerWindow)
    return error(context, request, x11::CoreErrorCode::BadWindow, owner);
  if (change.status == SelectionOwnershipStatus::InvalidSelection)
    return error(context, request, x11::CoreErrorCode::BadAtom, selection);

  DispatchResult result;
  append_xfixes_notifications(
      result, state.selections().xfixes_notifications(
                  selection, 0, owner, context.input.logical_time,
                  change.effective_time));
  if (change.previous_owner &&
      (owner == 0 || change.previous_owner->client != context.client_id ||
       change.previous_owner->window != owner)) {
    result.protocol_events.push_back(direct_event(
        change.previous_owner->client,
        x11::SelectionClearEvent{change.effective_time,
                                 change.previous_owner->window, selection}));
  }
  return result;
}

DispatchResult get_selection_owner(ServerState& state,
                                   const DispatchContext& context,
                                   const x11::FramedRequest& request) {
  if (!exact_size(request, 8))
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t selection = 0;
  if (!reader.read_u32(selection))
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (!state.atoms().valid(selection))
    return error(context, request, x11::CoreErrorCode::BadAtom, selection);
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  const auto owner = state.selections().owner(selection);
  reply.write_u32(owner ? owner->window : 0);
  reply.write_padding(20);
  return {std::move(reply).finish()};
}

DispatchResult convert_selection(ServerState& state,
                                 const DispatchContext& context,
                                 const x11::FramedRequest& request) {
  if (!exact_size(request, 24))
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t requestor = 0, selection = 0, target = 0, property = 0,
                time = 0;
  if (!reader.read_u32(requestor) || !reader.read_u32(selection) ||
      !reader.read_u32(target) || !reader.read_u32(property) ||
      !reader.read_u32(time))
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (!state.resources().find_window(requestor))
    return error(context, request, x11::CoreErrorCode::BadWindow, requestor);
  for (const auto atom : {selection, target})
    if (!state.atoms().valid(atom))
      return error(context, request, x11::CoreErrorCode::BadAtom, atom);
  if (!state.atoms().valid(property, true))
    return error(context, request, x11::CoreErrorCode::BadAtom, property);

  const auto conversion = state.selections().convert(
      context.client_id, requestor, selection, target, property, time,
      context.input.logical_time);
  DispatchResult result;
  if (conversion.kind == SelectionConversionKind::NotifyNoOwner) {
    result.protocol_events.push_back(direct_event(
        context.client_id,
        x11::SelectionNotifyEvent{conversion.time, requestor, selection,
                                  target, 0, false}));
  } else {
    result.protocol_events.push_back(direct_event(
        conversion.owner->client,
        x11::SelectionRequestEvent{conversion.time, conversion.owner->window,
                                   requestor, selection, target, property}));
  }
  return result;
}

DispatchResult send_event(ServerState& state, const DispatchContext& context,
                          const x11::FramedRequest& request) {
  if (!exact_size(request, 44))
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (request.data > 1)
    return error(context, request, x11::CoreErrorCode::BadValue, request.data);

  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t destination = 0, event_mask = 0;
  std::uint8_t raw_type = 0, detail = 0;
  std::uint16_t ignored_sequence = 0;
  if (!reader.read_u32(destination) || !reader.read_u32(event_mask) ||
      !reader.read_u8(raw_type) || !reader.read_u8(detail) ||
      !reader.read_u16(ignored_sequence))
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (!state.resources().find_window(destination))
    return error(context, request, x11::CoreErrorCode::BadWindow, destination);
  if ((event_mask & ~x11::event_mask::All) != 0)
    return error(context, request, x11::CoreErrorCode::BadValue, event_mask);

  const auto type = static_cast<x11::CoreEventType>(raw_type & 0x7fU);
  DispatchResult result;
  if (type == x11::CoreEventType::SelectionNotify) {
    std::uint32_t time = 0, requestor = 0, selection = 0, target = 0,
                  property = 0;
    if (detail != 0 || event_mask != 0 || request.data != 0 ||
        !reader.read_u32(time) || !reader.read_u32(requestor) ||
        !reader.read_u32(selection) || !reader.read_u32(target) ||
        !reader.read_u32(property) || !reader.skip(8))
      return error(context, request, x11::CoreErrorCode::BadValue,
                   static_cast<std::uint32_t>(raw_type));
    if (requestor != destination || !state.resources().find_window(requestor))
      return error(context, request, x11::CoreErrorCode::BadWindow, requestor);
    for (const auto atom : {selection, target})
      if (!state.atoms().valid(atom))
        return error(context, request, x11::CoreErrorCode::BadAtom, atom);
    if (!state.atoms().valid(property, true))
      return error(context, request, x11::CoreErrorCode::BadAtom, property);
    const auto owner = state.selections().owner(selection);
    if (!owner || owner->client != context.client_id)
      return error(context, request, x11::CoreErrorCode::BadAccess,
                   selection);
    result.protocol_events.push_back(window_event(
        destination, 0, false,
        x11::SelectionNotifyEvent{time, requestor, selection, target, property,
                                  true}));
    return result;
  }

  if (type == x11::CoreEventType::UnmapNotify) {
    std::uint32_t event_window = 0, window = 0;
    std::uint8_t from_configure = 0;
    constexpr auto kWithdrawMask = x11::event_mask::SubstructureNotify |
                                   x11::event_mask::SubstructureRedirect;
    if (detail != 0 || request.data != 0 || event_mask != kWithdrawMask ||
        !reader.read_u32(event_window) || !reader.read_u32(window) ||
        !reader.read_u8(from_configure) || !reader.skip(19) ||
        event_window != destination || from_configure != 0)
      return error(context, request, x11::CoreErrorCode::BadValue,
                   static_cast<std::uint32_t>(raw_type));
    const auto* record = state.resources().find_window(window);
    if (!record || record->parent != destination)
      return error(context, request, x11::CoreErrorCode::BadWindow, window);
    result.protocol_events.push_back(window_event(
        destination, event_mask, false,
        x11::UnmapNotifyEvent{event_window, window, false, true}));
    return result;
  }

  if (type != x11::CoreEventType::ClientMessage)
    return error(context, request, x11::CoreErrorCode::BadValue, raw_type);

  std::uint32_t window = 0, message_type = 0;
  if (!reader.read_u32(window) || !reader.read_u32(message_type))
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (!state.atoms().valid(message_type))
    return error(context, request, x11::CoreErrorCode::BadAtom, message_type);

  x11::ClientMessageEvent event;
  event.window = window;
  event.type = message_type;
  event.synthetic = true;
  if (detail == 8) {
    std::array<std::uint8_t, 20> data{};
    for (auto& value : data)
      if (!reader.read_u8(value))
        return error(context, request, x11::CoreErrorCode::BadLength);
    event.data = data;
  } else if (detail == 16) {
    std::array<std::uint16_t, 10> data{};
    for (auto& value : data)
      if (!reader.read_u16(value))
        return error(context, request, x11::CoreErrorCode::BadLength);
    event.data = data;
  } else if (detail == 32) {
    std::array<std::uint32_t, 5> data{};
    for (auto& value : data)
      if (!reader.read_u32(value))
        return error(context, request, x11::CoreErrorCode::BadLength);
    event.data = data;
  } else {
    return error(context, request, x11::CoreErrorCode::BadValue, detail);
  }
  if (auto handled = handle_ewmh_client_message(
          state, context, request, destination, event))
    return std::move(*handled);
  if (window != destination)
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  result.protocol_events.push_back(window_event(
      destination, event_mask, request.data != 0, std::move(event)));
  return result;
}

}  // namespace glasswyrm::server::request_handlers
