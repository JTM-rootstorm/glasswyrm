#include "glasswyrmd/request_handlers/common.hpp"

#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/reply.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_set>
#include <utility>

namespace glasswyrm::server::request_handlers {
namespace x11 = gw::protocol::x11;

DispatchResult query_tree(ServerState& state, const DispatchContext& context,
                          const x11::FramedRequest& request) {
  if (!exact_size(request, 8)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t window_id = 0;
  (void)reader.read_u32(window_id);
  const auto* window = state.resources().find_window(window_id);
  if (window == nullptr) {
    return error(context, request, x11::CoreErrorCode::BadWindow, window_id);
  }
  if (window->children.size() > std::numeric_limits<std::uint16_t>::max()) {
    return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u32(state.screen().root_window);
  reply.write_u32(window->parent);
  reply.write_u16(static_cast<std::uint16_t>(window->children.size()));
  reply.write_padding(14);
  for (const auto child : window->children) {
    reply.write_payload_u32(child);
  }
  return {std::move(reply).finish()};
}

DispatchResult get_input_focus(const ServerState& state,
                               const DispatchContext& context,
                               const x11::FramedRequest& request) {
  if (!exact_size(request, 4)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  x11::ReplyBuilder reply(context.byte_order, context.sequence, 0);
  reply.write_u32(state.focused_window());
  return {std::move(reply).finish()};
}

std::optional<std::pair<std::int64_t, std::int64_t>> window_root_origin(
    const ResourceTable& resources, std::uint32_t xid) {
  std::int64_t x = 0, y = 0;
  std::unordered_set<std::uint32_t> visited;
  while (xid != resources.screen().root_window) {
    if (!visited.insert(xid).second) return std::nullopt;
    const auto* window = resources.find_window(xid);
    if (!window) return std::nullopt;
    x += static_cast<std::int64_t>(window->x) + window->border_width;
    y += static_cast<std::int64_t>(window->y) + window->border_width;
    xid = window->parent;
  }
  return std::pair{x, y};
}

std::uint32_t immediate_child_at(const ResourceTable& resources,
                                 const WindowResource& parent,
                                 const std::int64_t root_x,
                                 const std::int64_t root_y) {
  for (auto iterator = parent.children.rbegin(); iterator != parent.children.rend(); ++iterator) {
    const auto* child = resources.find_window(*iterator);
    if (!child || child->map_state != MapState::Viewable) continue;
    const auto origin = window_root_origin(resources, *iterator);
    if (!origin) continue;
    const auto border = static_cast<std::int64_t>(child->border_width);
    const auto left = origin->first - border, top = origin->second - border;
    const auto right = left + child->width + border * 2;
    const auto bottom = top + child->height + border * 2;
    if (root_x >= left && root_x < right && root_y >= top && root_y < bottom)
      return *iterator;
  }
  return 0;
}

bool fits_i16(const std::int64_t value) {
  return value >= std::numeric_limits<std::int16_t>::min() &&
         value <= std::numeric_limits<std::int16_t>::max();
}

DispatchResult query_pointer(const ServerState& state,
                             const DispatchContext& context,
                             const x11::FramedRequest& request) {
  if (!exact_size(request, 8)) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order); std::uint32_t xid{};
  (void)reader.read_u32(xid);
  const auto* window = state.resources().find_window(xid);
  if (!window) return error(context, request, x11::CoreErrorCode::BadWindow, xid);
  const auto origin = window_root_origin(state.resources(), xid);
  if (!origin) return error(context, request, x11::CoreErrorCode::BadImplementation);
  const auto win_x = static_cast<std::int64_t>(context.input.root_x) - origin->first;
  const auto win_y = static_cast<std::int64_t>(context.input.root_y) - origin->second;
  if (!fits_i16(context.input.root_x) || !fits_i16(context.input.root_y) ||
      !fits_i16(win_x) || !fits_i16(win_y))
    return error(context, request, x11::CoreErrorCode::BadImplementation);
  x11::ReplyBuilder reply(context.byte_order, context.sequence, 1);
  reply.write_u32(state.screen().root_window);
  reply.write_u32(immediate_child_at(state.resources(), *window, context.input.root_x, context.input.root_y));
  reply.write_u16(static_cast<std::uint16_t>(context.input.root_x));
  reply.write_u16(static_cast<std::uint16_t>(context.input.root_y));
  reply.write_u16(static_cast<std::uint16_t>(win_x));
  reply.write_u16(static_cast<std::uint16_t>(win_y));
  reply.write_u16(context.input.state_mask); reply.write_padding(2);
  return {std::move(reply).finish()};
}

DispatchResult translate_coordinates(const ServerState& state,
                                     const DispatchContext& context,
                                     const x11::FramedRequest& request) {
  if (!exact_size(request, 16)) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t source{}, destination{}; std::uint16_t source_x_wire{}, source_y_wire{};
  if (!reader.read_u32(source) || !reader.read_u32(destination) ||
      !reader.read_u16(source_x_wire) || !reader.read_u16(source_y_wire))
    return error(context, request, x11::CoreErrorCode::BadLength);
  const auto* source_window = state.resources().find_window(source);
  if (!source_window) return error(context, request, x11::CoreErrorCode::BadWindow, source);
  const auto* destination_window = state.resources().find_window(destination);
  if (!destination_window) return error(context, request, x11::CoreErrorCode::BadWindow, destination);
  const auto source_origin = window_root_origin(state.resources(), source);
  const auto destination_origin = window_root_origin(state.resources(), destination);
  if (!source_origin || !destination_origin)
    return error(context, request, x11::CoreErrorCode::BadImplementation);
  const auto root_x = source_origin->first + static_cast<std::int16_t>(source_x_wire);
  const auto root_y = source_origin->second + static_cast<std::int16_t>(source_y_wire);
  const auto destination_x = root_x - destination_origin->first;
  const auto destination_y = root_y - destination_origin->second;
  if (!fits_i16(destination_x) || !fits_i16(destination_y))
    return error(context, request, x11::CoreErrorCode::BadImplementation);
  x11::ReplyBuilder reply(context.byte_order, context.sequence, 1);
  reply.write_u32(immediate_child_at(state.resources(), *destination_window, root_x, root_y));
  reply.write_u16(static_cast<std::uint16_t>(destination_x));
  reply.write_u16(static_cast<std::uint16_t>(destination_y));
  return {std::move(reply).finish()};
}

DispatchResult query_extension(const DispatchContext& context,
                               const x11::FramedRequest& request) {
  if (request.bytes.size() < 8) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint16_t name_length{};
  if (!reader.read_u16(name_length) || !reader.skip(2) ||
      !exact_size(request, 8 + padded_size(name_length)))
    return error(context, request, x11::CoreErrorCode::BadLength);
  std::span<const std::uint8_t> name;
  if (!reader.read_bytes(name_length, name)) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u8(0); reply.write_u8(0); reply.write_u8(0); reply.write_u8(0);
  return {std::move(reply).finish()};
}

DispatchResult list_extensions(const DispatchContext& context,
                               const x11::FramedRequest& request) {
  if (!exact_size(request, 4)) return error(context, request, x11::CoreErrorCode::BadLength);
  return {std::move(x11::ReplyBuilder(context.byte_order, context.sequence, 0)).finish()};
}

std::uint32_t keysym_for(const std::uint8_t keycode, const bool shifted) {
  constexpr std::array<std::pair<std::uint8_t, char>, 26> letters{{
    {38,'a'},{56,'b'},{54,'c'},{40,'d'},{26,'e'},{41,'f'},{42,'g'},{43,'h'},{31,'i'},{44,'j'},{45,'k'},{46,'l'},{58,'m'},{57,'n'},{32,'o'},{33,'p'},{24,'q'},{27,'r'},{39,'s'},{28,'t'},{30,'u'},{55,'v'},{25,'w'},{53,'x'},{29,'y'},{52,'z'}}};
  for (const auto [code, letter] : letters) if (keycode == code) return static_cast<std::uint32_t>(shifted ? letter - 32 : letter);
  if (keycode >= 10 && keycode <= 18) return shifted ? std::array<std::uint32_t,9>{0x21,0x40,0x23,0x24,0x25,0x5e,0x26,0x2a,0x28}[keycode-10] : 0x31U + keycode-10;
  if (keycode == 19) return shifted ? 0x29 : 0x30;
  switch (keycode) { case 9:return 0xff1b; case 22:return 0xff08; case 23:return 0xff09; case 36:return 0xff0d; case 37:return 0xffe3; case 50:return 0xffe1; case 62:return 0xffe2; case 64:return 0xffe9; case 65:return 0x20; case 105:return 0xffe4; case 108:return 0xffea; default:return 0; }
}

DispatchResult get_keyboard_mapping(const DispatchContext& context, const x11::FramedRequest& request) {
  if (!exact_size(request, 8)) return error(context, request, x11::CoreErrorCode::BadLength);
  const auto first = request.bytes[4], count = request.bytes[5];
  if (first < 8 || count == 0 || static_cast<unsigned>(first) + count > 256)
    return error(context, request, x11::CoreErrorCode::BadValue, first);
  if (const auto& mapping = context.input.keyboard_mapping) {
    const auto end = static_cast<unsigned>(first) + count;
    const auto required =
        (static_cast<std::size_t>(mapping->maximum_keycode) + 1U) *
        mapping->keysyms_per_keycode;
    if (mapping->keysyms_per_keycode == 0 ||
        mapping->keysyms.size() < required)
      return error(context, request, x11::CoreErrorCode::BadImplementation);
    if (first < mapping->minimum_keycode ||
        end > static_cast<unsigned>(mapping->maximum_keycode) + 1U)
      return error(context, request, x11::CoreErrorCode::BadValue, first);
    x11::ReplyBuilder reply(context.byte_order, context.sequence,
                            mapping->keysyms_per_keycode);
    for (unsigned keycode = first; keycode < end; ++keycode) {
      const auto offset =
          static_cast<std::size_t>(keycode) * mapping->keysyms_per_keycode;
      for (std::size_t level = 0; level < mapping->keysyms_per_keycode;
           ++level)
        reply.write_payload_u32(mapping->keysyms[offset + level]);
    }
    return {std::move(reply).finish()};
  }
  x11::ReplyBuilder reply(context.byte_order, context.sequence, 2);
  for (unsigned keycode = first; keycode < static_cast<unsigned>(first) + count; ++keycode) { reply.write_payload_u32(keysym_for(static_cast<std::uint8_t>(keycode), false)); reply.write_payload_u32(keysym_for(static_cast<std::uint8_t>(keycode), true)); }
  return {std::move(reply).finish()};
}

DispatchResult get_pointer_mapping(const DispatchContext& context, const x11::FramedRequest& request) {
  if (!exact_size(request, 4)) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ReplyBuilder reply(context.byte_order, context.sequence, 5); constexpr std::array<std::uint8_t,5> map{1,2,3,4,5}; reply.write_payload(map); return {std::move(reply).finish()};
}

DispatchResult get_modifier_mapping(const DispatchContext& context, const x11::FramedRequest& request) {
  if (!exact_size(request, 4)) return error(context, request, x11::CoreErrorCode::BadLength);
  if (const auto& mapping = context.input.keyboard_mapping) {
    const auto required =
        static_cast<std::size_t>(mapping->keycodes_per_modifier) * 8U;
    if (mapping->keycodes_per_modifier == 0 ||
        mapping->modifier_keycodes.size() != required)
      return error(context, request, x11::CoreErrorCode::BadImplementation);
    x11::ReplyBuilder reply(context.byte_order, context.sequence,
                            mapping->keycodes_per_modifier);
    reply.write_payload(mapping->modifier_keycodes);
    return {std::move(reply).finish()};
  }
  x11::ReplyBuilder reply(context.byte_order, context.sequence, 2); constexpr std::array<std::uint8_t,16> map{50,62, 0,0, 37,105, 64,108, 0,0, 0,0, 0,0, 0,0}; reply.write_payload(map); return {std::move(reply).finish()};
}

}  // namespace glasswyrm::server::request_handlers
