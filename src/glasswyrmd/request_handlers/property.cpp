#include "glasswyrmd/request_handlers/common.hpp"

#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/event_mask.hpp"
#include "protocol/x11/reply.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace glasswyrm::server::request_handlers {
namespace x11 = gw::protocol::x11;

namespace {

DispatchResult property_result(const DispatchContext& context,
                               const std::uint32_t window,
                               const std::uint32_t atom,
                               const x11::PropertyNotifyState state,
                               std::vector<std::uint8_t> output = {}) {
  DispatchResult result(std::move(output));
  result.protocol_events.push_back(
      {ProtocolEventDelivery::WindowMask, 0, window,
       x11::event_mask::PropertyChange, false,
       x11::PropertyNotifyEvent{window, atom, context.input.logical_time,
                                state}});
  return result;
}

}  // namespace

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
    case PropertyMutationStatus::Success:
      return property_result(context, window, property_atom,
                             x11::PropertyNotifyState::NewValue);
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
  const bool existed =
      state.resources().find_window(window)->properties.contains(property_atom);
  (void)state.resources().delete_property(window, property_atom);
  return existed ? property_result(context, window, property_atom,
                                   x11::PropertyNotifyState::Deleted)
                 : DispatchResult{};
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
  auto output = std::move(reply).finish();
  return result.deleted
             ? property_result(context, window, property_atom,
                               x11::PropertyNotifyState::Deleted,
                               std::move(output))
             : DispatchResult(std::move(output));
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


}  // namespace glasswyrm::server::request_handlers
