#include "glasswyrmd/extensions/gw_vrr.hpp"

#include "glasswyrmd/request_handlers/common.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/reply.hpp"

#include <algorithm>
#include <new>

namespace glasswyrm::server::extensions {
namespace x11 = gw::protocol::x11;
using request_handlers::error;

namespace {

DispatchResult extension_error(const DispatchContext& context,
                               const x11::FramedRequest& request,
                               const std::uint8_t code,
                               const std::uint32_t bad_value) {
  return {x11::encode_core_error(
      context.byte_order,
      {static_cast<x11::CoreErrorCode>(code), context.sequence, bad_value,
       request.opcode, request.data})};
}

WindowResource* eligible_window(ServerState& state,
                                const DispatchContext& context,
                                const x11::FramedRequest& request,
                                const std::uint32_t xid,
                                const bool require_owner,
                                DispatchResult& failure) {
  auto* window = state.resources().find_window(xid);
  const auto* resource = state.resources().find(xid);
  if (!window || window->parent != state.screen().root_window ||
      window->window_class != WindowClass::InputOutput ||
      (require_owner &&
       (!resource || resource->owner != context.client_id))) {
    failure = extension_error(context, request, kGwVrrBadWindow, xid);
    return nullptr;
  }
  return window;
}

VrrDispatchResult query_version(const DispatchContext& context,
                                const x11::FramedRequest& request) {
  if (request.core_size() != 12)
    return {error(context, request, x11::CoreErrorCode::BadLength), {}};
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t client_major{}, client_minor{};
  (void)reader.read_u32(client_major);
  (void)reader.read_u32(client_minor);
  const auto minor =
      client_major == 0 ? std::min(client_minor, UINT32_C(1)) : UINT32_C(1);
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u32(0);
  reply.write_u32(minor);
  reply.write_padding(16);
  return {{std::move(reply).finish()}, {}};
}

VrrDispatchResult select_input(ServerState& state, VrrWindowStateStore& vrr,
                               const DispatchContext& context,
                               const x11::FramedRequest& request) {
  if (request.core_size() != 12)
    return {error(context, request, x11::CoreErrorCode::BadLength), {}};
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{}, mask{};
  (void)reader.read_u32(xid);
  (void)reader.read_u32(mask);
  DispatchResult failure;
  if (!eligible_window(state, context, request, xid, true, failure))
    return {std::move(failure), {}};
  if ((mask & ~kKnownVrrEventMask) != 0)
    return {error(context, request, x11::CoreErrorCode::BadValue, mask), {}};
  auto& tracked = vrr.ensure_window(xid);
  if (mask == 0)
    tracked.event_selections.erase(context.client_id);
  else {
    try {
      tracked.event_selections.insert_or_assign(context.client_id, mask);
    } catch (const std::bad_alloc&) {
      return {error(context, request, x11::CoreErrorCode::BadAlloc), {}};
    }
  }
  return {};
}

VrrDispatchResult get_window_preference(
    ServerState& state, VrrWindowStateStore& vrr,
    const DispatchContext& context, const x11::FramedRequest& request) {
  if (request.core_size() != 8)
    return {error(context, request, x11::CoreErrorCode::BadLength), {}};
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{};
  (void)reader.read_u32(xid);
  DispatchResult failure;
  if (!eligible_window(state, context, request, xid, false, failure))
    return {std::move(failure), {}};
  const auto* tracked = vrr.find_window(xid);
  const auto preference = tracked ? tracked->preference
                                  : WindowVrrPreference::Default;
  const auto output = tracked ? tracked->primary_output : 0U;
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u32(xid);
  reply.write_u16(static_cast<std::uint16_t>(preference));
  reply.write_padding(2);
  reply.write_u32(output);
  reply.write_padding(12);
  return {{std::move(reply).finish()}, {}};
}

VrrDispatchResult set_window_preference(
    ServerState& state, VrrWindowStateStore& vrr,
    const DispatchContext& context, const x11::FramedRequest& request) {
  if (request.core_size() != 12)
    return {error(context, request, x11::CoreErrorCode::BadLength), {}};
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{}, raw{};
  (void)reader.read_u32(xid);
  (void)reader.read_u32(raw);
  DispatchResult failure;
  if (!eligible_window(state, context, request, xid, true, failure))
    return {std::move(failure), {}};
  if (raw > UINT16_MAX || !valid_vrr_preference(static_cast<std::uint16_t>(raw)))
    return {extension_error(context, request, kGwVrrBadPreference, raw), {}};
  const auto preference = static_cast<WindowVrrPreference>(raw);
  const auto* tracked = vrr.find_window(xid);
  const auto output = tracked ? tracked->primary_output : 0U;
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u16(static_cast<std::uint16_t>(preference));
  reply.write_padding(2);
  reply.write_u32(output);
  reply.write_u32(xid);
  reply.write_padding(12);
  return {{std::move(reply).finish()}, VrrPreferenceChange{xid, preference}};
}

VrrDispatchResult get_window_state(ServerState& state,
                                   VrrWindowStateStore& vrr,
                                   const DispatchContext& context,
                                   const x11::FramedRequest& request) {
  if (request.core_size() != 8)
    return {error(context, request, x11::CoreErrorCode::BadLength), {}};
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{};
  (void)reader.read_u32(xid);
  DispatchResult failure;
  if (!eligible_window(state, context, request, xid, false, failure))
    return {std::move(failure), {}};
  const WindowVrrState empty;
  const auto* found = vrr.find_window(xid);
  const auto& value = found ? *found : empty;
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u32(xid);
  reply.write_u32(value.primary_output);
  reply.write_u16(static_cast<std::uint16_t>(value.preference));
  reply.write_u8(static_cast<std::uint8_t>(value.policy_eligible));
  reply.write_u8(static_cast<std::uint8_t>(value.selected_candidate));
  reply.write_u8(static_cast<std::uint8_t>(value.effective_output_enabled));
  reply.write_padding(3);
  reply.write_u32(static_cast<std::uint32_t>(value.reason_flags >> 32U));
  reply.write_u32(static_cast<std::uint32_t>(value.reason_flags));
  reply.write_payload_u32(static_cast<std::uint32_t>(value.policy_generation >> 32U));
  reply.write_payload_u32(static_cast<std::uint32_t>(value.policy_generation));
  reply.write_payload_u32(
      static_cast<std::uint32_t>(value.output_state_generation >> 32U));
  reply.write_payload_u32(static_cast<std::uint32_t>(value.output_state_generation));
  return {{std::move(reply).finish()}, {}};
}

VrrDispatchResult get_output_state(ServerState& state,
                                   VrrWindowStateStore& vrr,
                                   const DispatchContext& context,
                                   const x11::FramedRequest& request) {
  if (request.core_size() != 8)
    return {error(context, request, x11::CoreErrorCode::BadLength), {}};
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{};
  (void)reader.read_u32(xid);
  if (!state.randr().output_model_enabled() || !state.randr().find_output(xid))
    return {extension_error(context, request, kGwVrrBadWindow, xid), {}};
  const PublishedOutputVrrState empty;
  const auto* found = vrr.find_output(xid);
  const auto& value = found ? *found : empty;
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u32(xid);
  reply.write_u16(static_cast<std::uint16_t>(value.policy));
  reply.write_u8(static_cast<std::uint8_t>(value.connector_property_present));
  reply.write_u8(static_cast<std::uint8_t>(value.hardware_capable));
  reply.write_u8(static_cast<std::uint8_t>(value.kms_controllable));
  reply.write_u8(static_cast<std::uint8_t>(value.simulated));
  reply.write_u8(static_cast<std::uint8_t>(value.effective_enabled));
  reply.write_u8(static_cast<std::uint8_t>(value.range_available));
  reply.write_u32(value.minimum_refresh_millihertz);
  reply.write_u32(value.maximum_refresh_millihertz);
  reply.write_u32(value.candidate_window);
  reply.write_payload_u32(static_cast<std::uint32_t>(value.reason_flags >> 32U));
  reply.write_payload_u32(static_cast<std::uint32_t>(value.reason_flags));
  reply.write_payload_u32(static_cast<std::uint32_t>(value.state_generation >> 32U));
  reply.write_payload_u32(static_cast<std::uint32_t>(value.state_generation));
  reply.write_payload_u32(
      static_cast<std::uint32_t>(value.latest_interval_nanoseconds >> 32U));
  reply.write_payload_u32(
      static_cast<std::uint32_t>(value.latest_interval_nanoseconds));
  return {{std::move(reply).finish()}, {}};
}

}  // namespace

VrrDispatchResult dispatch_gw_vrr(
    ServerState& state, VrrWindowStateStore& vrr,
    const DispatchContext& context, const x11::FramedRequest& request) {
  switch (request.data) {
    case 0: return query_version(context, request);
    case 1: return select_input(state, vrr, context, request);
    case 2: return get_window_preference(state, vrr, context, request);
    case 3: return set_window_preference(state, vrr, context, request);
    case 4: return get_window_state(state, vrr, context, request);
    case 5: return get_output_state(state, vrr, context, request);
    default:
      return {error(context, request, x11::CoreErrorCode::BadRequest), {}};
  }
}

std::vector<std::uint8_t> encode_gw_vrr_notify(
    const x11::ByteOrder order, const std::uint64_t sequence,
    const std::uint32_t change_mask, const std::uint32_t window,
    const WindowVrrState& state, const OutputVrrPolicyMode policy) {
  x11::ByteWriter event(order);
  const auto detail = static_cast<std::uint8_t>(
      (change_mask & kKnownVrrEventMask) |
      (state.effective_output_enabled ? UINT32_C(0x80) : 0));
  event.write_u8(kGwVrrEventBase);
  event.write_u8(detail);
  event.write_u16(x11::wire_sequence(sequence));
  event.write_u32(window);
  event.write_u32(state.primary_output);
  event.write_u16(static_cast<std::uint16_t>(state.preference));
  event.write_u16(static_cast<std::uint16_t>(policy));
  event.write_u32(static_cast<std::uint32_t>(state.reason_flags >> 32U));
  event.write_u32(static_cast<std::uint32_t>(state.reason_flags));
  event.write_u32(static_cast<std::uint32_t>(state.output_state_generation >> 32U));
  event.write_u32(static_cast<std::uint32_t>(state.output_state_generation));
  return std::move(event).take();
}

std::vector<VrrNotification> gw_vrr_notifications(
    const x11::ByteOrder order, const std::uint64_t sequence,
    const std::uint32_t window, const WindowVrrState& before,
    const WindowVrrState& after, const OutputVrrPolicyMode policy) {
  std::vector<VrrNotification> result;
  const auto changed = vrr_change_mask(before, after);
  if (changed == 0) return result;
  for (const auto& [client, selection] : after.event_selections) {
    const auto selected = changed & selection & kKnownVrrEventMask;
    if (selected != 0)
      result.push_back({client, encode_gw_vrr_notify(
                                    order, sequence, selected, window, after,
                                    policy)});
  }
  return result;
}

std::vector<std::uint8_t> gw_vrr_lifecycle_completion(
    const x11::ByteOrder order, const std::uint64_t sequence,
    const DeferredVrrMutation& mutation, const bool accepted) {
  if (accepted) return mutation.accepted_reply;
  return x11::encode_core_error(
      order, {x11::CoreErrorCode::BadImplementation, sequence, mutation.window,
              kGwVrrMajorOpcode, 3});
}

}  // namespace glasswyrm::server::extensions
