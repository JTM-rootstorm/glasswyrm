#include "glasswyrmd/request_dispatcher.hpp"

#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/core.hpp"
#include "protocol/x11/reply.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace glasswyrm::server {
namespace x11 = gw::protocol::x11;
namespace {

DispatchResult error(const DispatchContext& context,
                     const x11::FramedRequest& request,
                     const x11::CoreErrorCode code,
                     const std::uint32_t bad_value = 0) {
  return {x11::encode_core_error(
      context.byte_order,
      {code, context.sequence, bad_value, request.opcode, 0})};
}

bool exact_size(const x11::FramedRequest& request, const std::size_t size) {
  return request.bytes.size() == size;
}

std::uint32_t property_bad_atom(const ServerState& state,
                                const std::uint32_t property,
                                const std::uint32_t type,
                                const bool allow_any_type) {
  if (!state.atoms().valid(property)) {
    return property;
  }
  if (!state.atoms().valid(type, allow_any_type)) {
    return type;
  }
  return 0;
}

DispatchResult create_window(ServerState& state, const DispatchContext& context,
                             const x11::FramedRequest& request) {
  constexpr std::uint32_t kKnownMask = 0x00007fffU;
  if (request.bytes.size() < 32) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  x11::ByteReader reader(request.bytes, context.byte_order);
  WindowCreateSpec spec;
  std::uint16_t x = 0;
  std::uint16_t y = 0;
  std::uint16_t window_class = 0;
  std::uint32_t value_mask = 0;
  if (!reader.skip(4) || !reader.read_u32(spec.xid) ||
      !reader.read_u32(spec.parent) || !reader.read_u16(x) ||
      !reader.read_u16(y) || !reader.read_u16(spec.width) ||
      !reader.read_u16(spec.height) || !reader.read_u16(spec.border_width) ||
      !reader.read_u16(window_class) || !reader.read_u32(spec.visual) ||
      !reader.read_u32(value_mask)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  spec.x = static_cast<std::int16_t>(x);
  spec.y = static_cast<std::int16_t>(y);
  spec.depth = request.data;
  spec.window_class = static_cast<WindowClass>(window_class);
  spec.attribute_mask = value_mask;
  if (window_class > static_cast<std::uint16_t>(WindowClass::InputOnly)) {
    return error(context, request, x11::CoreErrorCode::BadValue, window_class);
  }
  if ((value_mask & ~kKnownMask) != 0) {
    return error(context, request, x11::CoreErrorCode::BadValue, value_mask);
  }
  const std::size_t value_count =
      static_cast<std::size_t>(std::popcount(value_mask));
  if (value_count > (std::numeric_limits<std::size_t>::max() - 32) / 4 ||
      !exact_size(request, 32 + value_count * 4)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }

  for (std::uint32_t bit = 0; bit < 15; ++bit) {
    if ((value_mask & (std::uint32_t{1} << bit)) == 0) {
      continue;
    }
    std::uint32_t value = 0;
    if (!reader.read_u32(value)) {
      return error(context, request, x11::CoreErrorCode::BadLength);
    }
    switch (bit) {
      case 0:
        if (value > 1) {
          return error(context, request, x11::CoreErrorCode::BadPixmap, value);
        }
        spec.attributes.background_pixmap = value;
        break;
      case 1: spec.attributes.background_pixel = value; break;
      case 2:
        if (value != 0) {
          return error(context, request, x11::CoreErrorCode::BadPixmap, value);
        }
        spec.attributes.border_pixmap = value;
        break;
      case 3: spec.attributes.border_pixel = value; break;
      case 4:
        if (value > 10) {
          return error(context, request, x11::CoreErrorCode::BadValue, value);
        }
        spec.attributes.bit_gravity = static_cast<std::uint8_t>(value);
        break;
      case 5:
        if (value > 10) {
          return error(context, request, x11::CoreErrorCode::BadValue, value);
        }
        spec.attributes.window_gravity = static_cast<std::uint8_t>(value);
        break;
      case 6:
        if (value > 2) {
          return error(context, request, x11::CoreErrorCode::BadValue, value);
        }
        spec.attributes.backing_store = static_cast<std::uint8_t>(value);
        break;
      case 7: spec.attributes.backing_planes = value; break;
      case 8: spec.attributes.backing_pixel = value; break;
      case 9:
        if (value > 1) {
          return error(context, request, x11::CoreErrorCode::BadValue, value);
        }
        spec.attributes.override_redirect = value != 0;
        break;
      case 10:
        if (value > 1) {
          return error(context, request, x11::CoreErrorCode::BadValue, value);
        }
        spec.attributes.save_under = value != 0;
        break;
      case 11:
        if ((value & ~0x01ffffffU) != 0) {
          return error(context, request, x11::CoreErrorCode::BadValue, value);
        }
        spec.attributes.event_mask = value;
        break;
      case 12:
        if ((value & ~0x0000204fU) != 0) {
          return error(context, request, x11::CoreErrorCode::BadValue, value);
        }
        spec.attributes.do_not_propagate_mask = value;
        break;
      case 13:
        if (value != 0 && value != state.screen().default_colormap) {
          return error(context, request, x11::CoreErrorCode::BadColormap,
                       value);
        }
        spec.attributes.colormap = value;
        break;
      case 14:
        if (value != 0) {
          return error(context, request, x11::CoreErrorCode::BadCursor, value);
        }
        spec.attributes.cursor = value;
        break;
      default: break;
    }
  }

  switch (state.resources().create_window(
      context.client_id, context.resource_base, context.resource_mask, spec)) {
    case CreateWindowStatus::Success: return {};
    case CreateWindowStatus::BadIdChoice:
      return error(context, request, x11::CoreErrorCode::BadIDChoice, spec.xid);
    case CreateWindowStatus::BadWindow:
      return error(context, request, x11::CoreErrorCode::BadWindow, spec.parent);
    case CreateWindowStatus::BadValue:
      return error(context, request, x11::CoreErrorCode::BadValue, 0);
    case CreateWindowStatus::BadMatch:
      return error(context, request, x11::CoreErrorCode::BadMatch);
    case CreateWindowStatus::BadAlloc:
      return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return error(context, request, x11::CoreErrorCode::BadImplementation);
}

DispatchResult destroy_window(ServerState& state,
                              const DispatchContext& context,
                              const x11::FramedRequest& request) {
  if (!exact_size(request, 8)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t window = 0;
  if (!reader.read_u32(window)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  const auto status = state.resources().destroy_window(window);
  if (status == DestroyWindowStatus::BadWindow) {
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  }
  return {};
}

DispatchResult get_geometry(ServerState& state,
                            const DispatchContext& context,
                            const x11::FramedRequest& request) {
  if (!exact_size(request, 8)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t drawable = 0;
  (void)reader.read_u32(drawable);
  const auto* window = state.resources().find_window(drawable);
  if (window == nullptr) {
    return error(context, request, x11::CoreErrorCode::BadDrawable, drawable);
  }
  x11::ReplyBuilder reply(context.byte_order, context.sequence, window->depth);
  reply.write_u32(state.screen().root_window);
  reply.write_u16(static_cast<std::uint16_t>(window->x));
  reply.write_u16(static_cast<std::uint16_t>(window->y));
  reply.write_u16(window->width);
  reply.write_u16(window->height);
  reply.write_u16(window->border_width);
  reply.write_padding(2);
  return {std::move(reply).finish()};
}

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

DispatchResult intern_atom(ServerState& state, const DispatchContext& context,
                           const x11::FramedRequest& request) {
  if (request.data > 1 || request.bytes.size() < 8) {
    return request.data > 1
               ? error(context, request, x11::CoreErrorCode::BadValue,
                       request.data)
               : error(context, request, x11::CoreErrorCode::BadLength);
  }
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint16_t name_length = 0;
  std::span<const std::uint8_t> name;
  if (!reader.read_u16(name_length) || !reader.skip(2) ||
      request.bytes.size() != 8 + ((name_length + 3U) & ~std::size_t{3}) ||
      !reader.read_bytes(name_length, name)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  const auto result = state.atoms().intern(
      std::string_view(reinterpret_cast<const char*>(name.data()), name.size()),
      request.data != 0);
  if (result.status == InternAtomStatus::Exhausted) {
    return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u32(result.atom);
  return {std::move(reply).finish()};
}

DispatchResult get_atom_name(ServerState& state,
                             const DispatchContext& context,
                             const x11::FramedRequest& request) {
  if (!exact_size(request, 8)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t atom = 0;
  (void)reader.read_u32(atom);
  const auto name = state.atoms().name(atom);
  if (!name) {
    return error(context, request, x11::CoreErrorCode::BadAtom, atom);
  }
  if (name->size() > std::numeric_limits<std::uint16_t>::max()) {
    return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u16(static_cast<std::uint16_t>(name->size()));
  reply.write_padding(22);
  reply.write_payload(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(name->data()), name->size()));
  return {std::move(reply).finish()};
}

std::optional<PropertyData> decode_property_data(
    x11::ByteReader& reader, const std::uint8_t format,
    const std::uint32_t item_count) {
  try {
    if (format == 8) {
      std::span<const std::uint8_t> bytes;
      if (!reader.read_bytes(item_count, bytes)) {
        return std::nullopt;
      }
      return PropertyData(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    }
    if (format == 16) {
      std::vector<std::uint16_t> values;
      values.reserve(item_count);
      for (std::uint32_t index = 0; index < item_count; ++index) {
        std::uint16_t value = 0;
        if (!reader.read_u16(value)) return std::nullopt;
        values.push_back(value);
      }
      return PropertyData(std::move(values));
    }
    if (format == 32) {
      std::vector<std::uint32_t> values;
      values.reserve(item_count);
      for (std::uint32_t index = 0; index < item_count; ++index) {
        std::uint32_t value = 0;
        if (!reader.read_u32(value)) return std::nullopt;
        values.push_back(value);
      }
      return PropertyData(std::move(values));
    }
  } catch (const std::bad_alloc&) {
    return std::nullopt;
  }
  return std::nullopt;
}

DispatchResult change_property(ServerState& state,
                               const DispatchContext& context,
                               const x11::FramedRequest& request) {
  if (request.bytes.size() < 24) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  if (request.data > 2) {
    return error(context, request, x11::CoreErrorCode::BadValue, request.data);
  }
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t window = 0;
  std::uint32_t property_atom = 0;
  std::uint32_t type_atom = 0;
  std::uint8_t format = 0;
  std::uint32_t item_count = 0;
  if (!reader.read_u32(window) || !reader.read_u32(property_atom) ||
      !reader.read_u32(type_atom) || !reader.read_u8(format) ||
      !reader.skip(3) || !reader.read_u32(item_count)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  if (format != 8 && format != 16 && format != 32) {
    return error(context, request, x11::CoreErrorCode::BadValue, format);
  }
  const std::uint64_t data_size64 =
      static_cast<std::uint64_t>(item_count) * (format / 8U);
  const std::uint64_t padded64 = (data_size64 + 3U) & ~std::uint64_t{3};
  if (padded64 > std::numeric_limits<std::size_t>::max() ||
      request.bytes.size() != 24 + static_cast<std::size_t>(padded64)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  if (state.resources().find_window(window) == nullptr) {
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  }
  if (const auto bad =
          property_bad_atom(state, property_atom, type_atom, false);
      bad != 0) {
    return error(context, request, x11::CoreErrorCode::BadAtom, bad);
  }
  auto data = decode_property_data(reader, format, item_count);
  if (!data) {
    return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  const auto status = state.resources().change_property(
      window, property_atom, Property{type_atom, std::move(*data)},
      static_cast<PropertyMode>(request.data));
  switch (status) {
    case PropertyMutationStatus::Success: return {};
    case PropertyMutationStatus::BadWindow:
      return error(context, request, x11::CoreErrorCode::BadWindow, window);
    case PropertyMutationStatus::BadMatch:
      return error(context, request, x11::CoreErrorCode::BadMatch);
    case PropertyMutationStatus::BadAlloc:
      return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return error(context, request, x11::CoreErrorCode::BadImplementation);
}

DispatchResult delete_property(ServerState& state,
                               const DispatchContext& context,
                               const x11::FramedRequest& request) {
  if (!exact_size(request, 12)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t window = 0;
  std::uint32_t property_atom = 0;
  (void)reader.read_u32(window);
  (void)reader.read_u32(property_atom);
  if (state.resources().find_window(window) == nullptr) {
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  }
  if (!state.atoms().valid(property_atom)) {
    return error(context, request, x11::CoreErrorCode::BadAtom, property_atom);
  }
  (void)state.resources().delete_property(window, property_atom);
  return {};
}

template <typename Values>
void write_property_payload(x11::ReplyBuilder& reply, const Values& values) {
  using Value = typename Values::value_type;
  for (const auto value : values) {
    if constexpr (sizeof(Value) == 1) {
      const std::uint8_t byte = value;
      reply.write_payload(std::span<const std::uint8_t>(&byte, 1));
    } else if constexpr (sizeof(Value) == 2) {
      reply.write_payload_u16(value);
    } else {
      reply.write_payload_u32(value);
    }
  }
}

DispatchResult get_property(ServerState& state, const DispatchContext& context,
                            const x11::FramedRequest& request) {
  if (!exact_size(request, 24)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  if (request.data > 1) {
    return error(context, request, x11::CoreErrorCode::BadValue, request.data);
  }
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t window = 0;
  std::uint32_t property_atom = 0;
  std::uint32_t type_atom = 0;
  std::uint32_t offset = 0;
  std::uint32_t length = 0;
  (void)reader.read_u32(window);
  (void)reader.read_u32(property_atom);
  (void)reader.read_u32(type_atom);
  (void)reader.read_u32(offset);
  (void)reader.read_u32(length);
  if (state.resources().find_window(window) == nullptr) {
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  }
  if (const auto bad =
          property_bad_atom(state, property_atom, type_atom, true);
      bad != 0) {
    return error(context, request, x11::CoreErrorCode::BadAtom, bad);
  }
  const auto result = state.resources().get_property(
      window, property_atom, type_atom, request.data != 0, offset, length);
  if (result.status == PropertyReadStatus::BadValue) {
    return error(context, request, x11::CoreErrorCode::BadValue, offset);
  }
  if (result.status == PropertyReadStatus::BadWindow) {
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  }
  const std::uint8_t format = result.present ? result.value.format : 0;
  x11::ReplyBuilder reply(context.byte_order, context.sequence, format);
  reply.write_u32(result.present ? result.value.type : 0);
  reply.write_u32(result.present ? result.value.bytes_after : 0);
  reply.write_u32(result.present && result.type_matched
                      ? static_cast<std::uint32_t>(result.value.item_count())
                      : 0);
  reply.write_padding(12);
  if (result.present && result.type_matched) {
    std::visit([&reply](const auto& values) {
      write_property_payload(reply, values);
    }, result.value.data);
  }
  return {std::move(reply).finish()};
}

DispatchResult list_properties(ServerState& state,
                               const DispatchContext& context,
                               const x11::FramedRequest& request) {
  if (!exact_size(request, 8)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t window = 0;
  (void)reader.read_u32(window);
  if (state.resources().find_window(window) == nullptr) {
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  }
  const auto atoms = state.resources().list_properties(window);
  if (atoms.size() > std::numeric_limits<std::uint16_t>::max()) {
    return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u16(static_cast<std::uint16_t>(atoms.size()));
  reply.write_padding(22);
  for (const auto atom : atoms) reply.write_payload_u32(atom);
  return {std::move(reply).finish()};
}

DispatchResult get_input_focus(const ServerState& state,
                               const DispatchContext& context,
                               const x11::FramedRequest& request) {
  if (!exact_size(request, 4)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  x11::ReplyBuilder reply(context.byte_order, context.sequence, 0);
  reply.write_u32(state.screen().root_window);
  return {std::move(reply).finish()};
}

}  // namespace

DispatchResult dispatch_request(ServerState& state,
                                const DispatchContext& context,
                                const x11::FramedRequest& request) {
  try {
    switch (static_cast<x11::CoreOpcode>(request.opcode)) {
      case x11::CoreOpcode::CreateWindow:
        return create_window(state, context, request);
      case x11::CoreOpcode::DestroyWindow:
        return destroy_window(state, context, request);
      case x11::CoreOpcode::GetGeometry:
        return get_geometry(state, context, request);
      case x11::CoreOpcode::QueryTree:
        return query_tree(state, context, request);
      case x11::CoreOpcode::InternAtom:
        return intern_atom(state, context, request);
      case x11::CoreOpcode::GetAtomName:
        return get_atom_name(state, context, request);
      case x11::CoreOpcode::ChangeProperty:
        return change_property(state, context, request);
      case x11::CoreOpcode::DeleteProperty:
        return delete_property(state, context, request);
      case x11::CoreOpcode::GetProperty:
        return get_property(state, context, request);
      case x11::CoreOpcode::ListProperties:
        return list_properties(state, context, request);
      case x11::CoreOpcode::GetInputFocus:
        return get_input_focus(state, context, request);
      case x11::CoreOpcode::NoOperation:
        return {};
    }
  } catch (const std::bad_alloc&) {
    return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return error(context, request, x11::CoreErrorCode::BadRequest);
}

}  // namespace glasswyrm::server
