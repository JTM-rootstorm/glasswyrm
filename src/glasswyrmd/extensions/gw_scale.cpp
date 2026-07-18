#include "glasswyrmd/extensions/gw_scale.hpp"

#include "glasswyrmd/extension_registry.hpp"
#include "glasswyrmd/extension_wire.hpp"
#include "glasswyrmd/randr_state.hpp"
#include "glasswyrmd/request_handlers/common.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/reply.hpp"

#include <algorithm>
#include <new>
#include <optional>

namespace glasswyrm::server::extensions {
namespace x11 = gw::protocol::x11;
using request_handlers::error;

namespace {

constexpr std::uint32_t kSupportedEventMask = UINT32_C(0x7);
constexpr std::uint16_t kLegacyScaleMode = 1;
constexpr std::uint16_t kScaledPixmapMode = 2;

DispatchResult extension_error(const DispatchContext& context,
                               const x11::FramedRequest& request,
                               const std::uint8_t relative_error,
                               const std::uint32_t bad_value) {
  const auto* extension = find_extension(ExtensionKind::GwScale);
  const auto packet =
      extension ? encode_extension_error(
                      context.byte_order, *extension, relative_error,
                      context.sequence, bad_value, request.opcode, request.data)
                : std::nullopt;
  return packet ? DispatchResult{*packet}
                : error(context, request,
                        x11::CoreErrorCode::BadImplementation);
}

WindowResource* eligible_window(ServerState& state,
                                const DispatchContext& context,
                                const x11::FramedRequest& request,
                                const std::uint32_t xid,
                                const bool require_owner,
                                DispatchResult& failure) {
  auto* window = state.resources().find_window(xid);
  if (!window) {
    failure = error(context, request, x11::CoreErrorCode::BadWindow, xid);
    return nullptr;
  }
  if (window->parent != state.screen().root_window ||
      window->window_class != WindowClass::InputOutput) {
    failure = error(context, request, x11::CoreErrorCode::BadMatch, xid);
    return nullptr;
  }
  const auto* record = state.resources().find(xid);
  if (require_owner && (!record || record->owner != context.client_id)) {
    failure = error(context, request, x11::CoreErrorCode::BadAccess, xid);
    return nullptr;
  }
  return window;
}

bool intersects_screen(const WindowResource& window,
                       const x11::ScreenModel& screen) {
  return window.x < static_cast<std::int64_t>(screen.width_pixels) &&
         window.y < static_cast<std::int64_t>(screen.height_pixels) &&
         static_cast<std::int64_t>(window.x) + window.width > 0 &&
         static_cast<std::int64_t>(window.y) + window.height > 0;
}

std::uint16_t wire_scale_mode(const WindowScaleState& scale) {
  return scale.presentation == WindowScalePresentationState::ScaleAwareActive
             ? kScaledPixmapMode
             : kLegacyScaleMode;
}

DispatchResult query_version(const DispatchContext& context,
                             const x11::FramedRequest& request) {
  if (request.core_size() != 12)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t client_major{};
  std::uint32_t client_minor{};
  (void)reader.read_u32(client_major);
  (void)reader.read_u32(client_minor);
  const auto negotiated_minor =
      client_major == 0 ? std::min(client_minor, UINT32_C(1)) : UINT32_C(1);
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u32(0);
  reply.write_u32(negotiated_minor);
  reply.write_padding(16);
  return {std::move(reply).finish()};
}

DispatchResult select_input(ServerState& state,
                            const DispatchContext& context,
                            const x11::FramedRequest& request) {
  if (request.core_size() != 12)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{}, mask{};
  (void)reader.read_u32(xid);
  (void)reader.read_u32(mask);
  DispatchResult failure;
  auto* window = eligible_window(state, context, request, xid, true, failure);
  if (!window) return failure;
  if ((mask & ~kSupportedEventMask) != 0)
    return error(context, request, x11::CoreErrorCode::BadValue, mask);
  if (mask == 0) {
    window->scale.event_selections.erase(context.client_id);
    return {};
  }
  try {
    window->scale.event_selections.insert_or_assign(context.client_id, mask);
    return {};
  } catch (const std::bad_alloc&) {
    return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
}

DispatchResult get_window_scale(ServerState& state,
                                const DispatchContext& context,
                                const x11::FramedRequest& request) {
  if (request.core_size() != 8)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{};
  (void)reader.read_u32(xid);
  DispatchResult failure;
  const auto* window =
      eligible_window(state, context, request, xid, false, failure);
  if (!window) return failure;
  const bool fixed_member = intersects_screen(*window, state.screen());
  const auto& scale = window->scale;
  const auto output = scale.has_output_state
                          ? scale.primary_output
                          : fixed_member ? kRandROutputId : UINT32_C(0);
  const auto preferred_numerator =
      scale.has_output_state ? scale.preferred_scale_numerator : UINT32_C(1);
  const auto preferred_denominator =
      scale.has_output_state ? scale.preferred_scale_denominator : UINT32_C(1);
  const auto generation = scale.has_output_state
                              ? scale.layout_generation
                              : std::uint64_t{kRandRConfigurationTimestamp};
  const auto membership_count = static_cast<std::uint16_t>(
      scale.has_output_state ? scale.output_memberships.size()
                             : fixed_member ? 1 : 0);
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u32(xid);
  reply.write_u32(output);
  reply.write_u32(preferred_numerator);
  reply.write_u32(preferred_denominator);
  reply.write_u32(scale.accepted_buffer_scale);
  reply.write_u16(wire_scale_mode(scale));
  reply.write_u16(membership_count);
  reply.write_payload_u32(
      static_cast<std::uint32_t>(generation >> 32U));
  reply.write_payload_u32(static_cast<std::uint32_t>(generation));
  if (scale.has_output_state) {
    for (const auto membership : scale.output_memberships)
      reply.write_payload_u32(membership);
  } else if (fixed_member) {
    reply.write_payload_u32(kRandROutputId);
  }
  return {std::move(reply).finish()};
}

DispatchResult set_window_buffer_scale(
    ServerState& state, const DispatchContext& context,
    const x11::FramedRequest& request) {
  if (request.core_size() != 12)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{}, requested_scale{};
  (void)reader.read_u32(xid);
  (void)reader.read_u32(requested_scale);
  DispatchResult failure;
  auto* window = eligible_window(state, context, request, xid, true, failure);
  if (!window) return failure;
  if (requested_scale == 0 || requested_scale > 4)
    return extension_error(context, request, 0, requested_scale);

  window->scale.accepted_buffer_scale = requested_scale;
  window->scale.presentation =
      WindowScalePresentationState::ScaleAwareAwaitingPixmap;
  const auto preferred_numerator = window->scale.has_output_state
                                       ? window->scale.preferred_scale_numerator
                                       : UINT32_C(1);
  const auto preferred_denominator =
      window->scale.has_output_state
          ? window->scale.preferred_scale_denominator
          : UINT32_C(1);
  const auto generation = window->scale.has_output_state
                              ? window->scale.layout_generation
                              : std::uint64_t{kRandRConfigurationTimestamp};
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u32(requested_scale);
  reply.write_u32(preferred_numerator);
  reply.write_u32(preferred_denominator);
  reply.write_u32(static_cast<std::uint32_t>(generation >> 32U));
  reply.write_u32(static_cast<std::uint32_t>(generation));
  reply.write_padding(4);
  return {std::move(reply).finish()};
}

DispatchResult reset_window_buffer_scale(
    ServerState& state, const DispatchContext& context,
    const x11::FramedRequest& request) {
  if (request.core_size() != 8)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{};
  (void)reader.read_u32(xid);
  DispatchResult failure;
  auto* window = eligible_window(state, context, request, xid, true, failure);
  if (!window) return failure;
  window->scale.accepted_buffer_scale = 1;
  window->scale.presentation = WindowScalePresentationState::Legacy;
  return {};
}

}  // namespace

DispatchResult dispatch_gw_scale(ServerState& state,
                                 const DispatchContext& context,
                                 const x11::FramedRequest& request) {
  switch (request.data) {
    case 0: return query_version(context, request);
    case 1: return select_input(state, context, request);
    case 3: return get_window_scale(state, context, request);
    case 4: return set_window_buffer_scale(state, context, request);
    case 6: return reset_window_buffer_scale(state, context, request);
    default:
      return error(context, request, x11::CoreErrorCode::BadRequest);
  }
}

}  // namespace glasswyrm::server::extensions
