#include "glasswyrmd/request_handlers/common.hpp"

#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/reply.hpp"

namespace glasswyrm::server::request_handlers {
namespace {

GrabMode mode(const std::uint8_t value) noexcept {
  return static_cast<GrabMode>(value);
}

DispatchResult grab_reply(const DispatchContext &context,
                          const GrabStatus status) {
  std::uint8_t wire = 0;
  if (status == GrabStatus::AlreadyGrabbed)
    wire = 1;
  else if (status == GrabStatus::InvalidTime)
    wire = 2;
  else if (status == GrabStatus::NotViewable)
    wire = 3;
  x11::ReplyBuilder reply(context.byte_order, context.sequence, wire);
  return {std::move(reply).finish()};
}

DispatchResult field_error(const DispatchContext &context,
                           const x11::FramedRequest &request,
                           const GrabStatus status,
                           const std::uint32_t cursor = 0) {
  if (status == GrabStatus::InvalidCursor)
    return error(context, request, x11::CoreErrorCode::BadCursor, cursor);
  if (status == GrabStatus::BadAccess)
    return error(context, request, x11::CoreErrorCode::BadAccess);
  if (status == GrabStatus::BadImplementation ||
      status == GrabStatus::UnsupportedConfine)
    return error(context, request, x11::CoreErrorCode::BadImplementation);
  return error(context, request, x11::CoreErrorCode::BadValue);
}

bool cursor_valid(const ServerState &state, const std::uint32_t cursor) {
  return cursor == 0 || state.resources().find_cursor(cursor) != nullptr;
}

std::shared_ptr<const input::CursorImage>
cursor_image(const ServerState &state, const std::uint32_t cursor) {
  if (cursor == 0)
    return {};
  const auto *resource = state.resources().find_cursor(cursor);
  return resource ? resource->image : nullptr;
}

} // namespace

DispatchResult grab_pointer(ServerState &state, const DispatchContext &context,
                            const x11::FramedRequest &request) {
  if (!exact_size(request, 24))
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (request.data > 1)
    return error(context, request, x11::CoreErrorCode::BadValue,
                 request.data);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t window{}, confine{}, cursor{}, time{};
  std::uint16_t event_mask{};
  std::uint8_t pointer_mode{}, keyboard_mode{};
  if (!reader.read_u32(window) || !reader.read_u16(event_mask) ||
      !reader.read_u8(pointer_mode) || !reader.read_u8(keyboard_mode) ||
      !reader.read_u32(confine) || !reader.read_u32(cursor) ||
      !reader.read_u32(time))
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (pointer_mode > 1)
    return error(context, request, x11::CoreErrorCode::BadValue, pointer_mode);
  if (keyboard_mode > 1)
    return error(context, request, x11::CoreErrorCode::BadValue,
                 keyboard_mode);
  const auto *resource = state.resources().find_window(window);
  if (!resource)
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  const auto status = state.grabs().grab_pointer(
      {context.client_id, window, request.data != 0, event_mask,
       mode(pointer_mode), mode(keyboard_mode), confine, cursor,
       cursor_valid(state, cursor), cursor_image(state, cursor), time,
       context.input.logical_time,
       resource->map_state == MapState::Viewable});
  if (status == GrabStatus::Success || status == GrabStatus::AlreadyGrabbed ||
      status == GrabStatus::InvalidTime || status == GrabStatus::NotViewable)
    return grab_reply(context, status);
  return field_error(context, request, status, cursor);
}

DispatchResult ungrab_pointer(ServerState &state,
                              const DispatchContext &context,
                              const x11::FramedRequest &request) {
  if (!exact_size(request, 8))
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t time{};
  (void)reader.read_u32(time);
  (void)state.grabs().ungrab_pointer(context.client_id, time,
                                    context.input.logical_time);
  return {};
}

DispatchResult grab_button(ServerState &state, const DispatchContext &context,
                           const x11::FramedRequest &request) {
  if (!exact_size(request, 24))
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (request.data > 1)
    return error(context, request, x11::CoreErrorCode::BadValue,
                 request.data);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t window{}, confine{}, cursor{};
  std::uint16_t event_mask{}, modifiers{};
  std::uint8_t pointer_mode{}, keyboard_mode{}, button{}, padding{};
  if (!reader.read_u32(window) || !reader.read_u16(event_mask) ||
      !reader.read_u8(pointer_mode) || !reader.read_u8(keyboard_mode) ||
      !reader.read_u32(confine) || !reader.read_u32(cursor) ||
      !reader.read_u8(button) || !reader.read_u8(padding) ||
      !reader.read_u16(modifiers))
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (pointer_mode > 1)
    return error(context, request, x11::CoreErrorCode::BadValue, pointer_mode);
  if (keyboard_mode > 1)
    return error(context, request, x11::CoreErrorCode::BadValue,
                 keyboard_mode);
  if (!state.resources().find_window(window))
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  if ((button != kAnyButton && (button < 1 || button > 9)) ||
      (modifiers != kAnyModifier && (modifiers & ~UINT16_C(0x00ff)) != 0))
    return error(context, request, x11::CoreErrorCode::BadValue,
                 button > 9 ? button : modifiers);
  const auto status = state.grabs().grab_button(
      {context.client_id, window, button, modifiers, request.data != 0,
       event_mask, mode(pointer_mode), mode(keyboard_mode), confine, cursor,
       cursor_valid(state, cursor), cursor_image(state, cursor)});
  return status == GrabStatus::Success
             ? DispatchResult{}
             : field_error(context, request, status, cursor);
}

DispatchResult ungrab_button(ServerState &state,
                             const DispatchContext &context,
                             const x11::FramedRequest &request) {
  if (!exact_size(request, 12))
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t window{};
  std::uint16_t modifiers{};
  if (!reader.read_u32(window) || !reader.read_u16(modifiers))
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (!state.resources().find_window(window))
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  if ((request.data != kAnyButton && request.data > 9) ||
      (modifiers != kAnyModifier && (modifiers & ~UINT16_C(0x00ff)) != 0))
    return error(context, request, x11::CoreErrorCode::BadValue,
                 request.data > 9 ? request.data : modifiers);
  (void)state.grabs().ungrab_button(context.client_id, window, request.data,
                                   modifiers);
  return {};
}

DispatchResult change_active_pointer_grab(
    ServerState &state, const DispatchContext &context,
    const x11::FramedRequest &request) {
  if (!exact_size(request, 16))
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t cursor{}, time{};
  std::uint16_t event_mask{};
  if (!reader.read_u32(cursor) || !reader.read_u32(time) ||
      !reader.read_u16(event_mask))
    return error(context, request, x11::CoreErrorCode::BadLength);
  const auto status = state.grabs().change_active_pointer_grab(
      context.client_id, event_mask, cursor, cursor_valid(state, cursor),
      cursor_image(state, cursor), time, context.input.logical_time);
  if (status == GrabStatus::Success || status == GrabStatus::NotFound ||
      status == GrabStatus::NotOwner || status == GrabStatus::InvalidTime)
    return {};
  return field_error(context, request, status, cursor);
}

DispatchResult grab_keyboard(ServerState &state,
                             const DispatchContext &context,
                             const x11::FramedRequest &request) {
  if (!exact_size(request, 16))
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (request.data > 1)
    return error(context, request, x11::CoreErrorCode::BadValue,
                 request.data);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t window{}, time{};
  std::uint8_t pointer_mode{}, keyboard_mode{};
  if (!reader.read_u32(window) || !reader.read_u32(time) ||
      !reader.read_u8(pointer_mode) || !reader.read_u8(keyboard_mode))
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (pointer_mode > 1)
    return error(context, request, x11::CoreErrorCode::BadValue, pointer_mode);
  if (keyboard_mode > 1)
    return error(context, request, x11::CoreErrorCode::BadValue,
                 keyboard_mode);
  const auto *resource = state.resources().find_window(window);
  if (!resource)
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  const auto status = state.grabs().grab_keyboard(
      {context.client_id, window, request.data != 0, mode(pointer_mode),
       mode(keyboard_mode), time, context.input.logical_time,
       resource->map_state == MapState::Viewable});
  if (status == GrabStatus::Success || status == GrabStatus::AlreadyGrabbed ||
      status == GrabStatus::InvalidTime || status == GrabStatus::NotViewable)
    return grab_reply(context, status);
  return field_error(context, request, status);
}

DispatchResult ungrab_keyboard(ServerState &state,
                               const DispatchContext &context,
                               const x11::FramedRequest &request) {
  if (!exact_size(request, 8))
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t time{};
  (void)reader.read_u32(time);
  (void)state.grabs().ungrab_keyboard(context.client_id, time,
                                     context.input.logical_time);
  return {};
}

DispatchResult allow_events(ServerState &state, const DispatchContext &context,
                            const x11::FramedRequest &request) {
  if (!exact_size(request, 8) || request.data > 7)
    return error(context, request, x11::CoreErrorCode::BadValue, request.data);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t time{};
  (void)reader.read_u32(time);
  const auto status = state.grabs().allow_events(
      static_cast<AllowEventsMode>(request.data), time,
      context.input.logical_time);
  return status == GrabStatus::BadImplementation
             ? error(context, request, x11::CoreErrorCode::BadImplementation)
             : DispatchResult{};
}

} // namespace glasswyrm::server::request_handlers
