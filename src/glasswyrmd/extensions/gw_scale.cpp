#include "glasswyrmd/extensions/gw_scale.hpp"

#include "glasswyrmd/extension_registry.hpp"
#include "glasswyrmd/extension_event_helpers.hpp"
#include "glasswyrmd/extension_wire.hpp"
#include "glasswyrmd/gw_scale_state.hpp"
#include "glasswyrmd/randr_state.hpp"
#include "glasswyrmd/request_handlers/common.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/reply.hpp"

#include <algorithm>
#include <array>
#include <new>
#include <optional>

namespace glasswyrm::server::extensions {
namespace x11 = gw::protocol::x11;
using request_handlers::error;

namespace {

constexpr std::uint32_t kSupportedEventMask = UINT32_C(0x7);
constexpr std::uint16_t kLegacyScaleMode = 1;
constexpr std::uint16_t kScaledPixmapMode = 2;

DispatchResult xfixes_region_error(const DispatchContext& context,
                                   const x11::FramedRequest& request,
                                   const std::uint32_t xid) {
  const auto* extension = find_extension(ExtensionKind::XFixes);
  const auto packet =
      extension ? encode_extension_error(context.byte_order, *extension, 0,
                                         context.sequence, xid, request.opcode,
                                         request.data)
                : std::nullopt;
  return packet ? DispatchResult{*packet}
                : error(context, request,
                        x11::CoreErrorCode::BadImplementation);
}

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

DispatchResult get_output_scale(ServerState& state,
                                const DispatchContext& context,
                                const x11::FramedRequest& request) {
  if (request.core_size() != 8)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{};
  (void)reader.read_u32(xid);
  const auto* output = state.randr().find_output(xid);
  if (!state.randr().output_model_enabled() || !output)
    return error(context, request, x11::CoreErrorCode::BadValue, xid);
  const auto generation = state.randr().output_layout()->generation;
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u32(static_cast<std::uint32_t>(output->internal_id >> 32U));
  reply.write_u32(static_cast<std::uint32_t>(output->internal_id));
  reply.write_u32(static_cast<std::uint32_t>(output->logical_x));
  reply.write_u32(static_cast<std::uint32_t>(output->logical_y));
  reply.write_u32(output->logical_width);
  reply.write_u32(output->logical_height);
  reply.write_payload_u32(output->physical_width);
  reply.write_payload_u32(output->physical_height);
  reply.write_payload_u32(output->scale.numerator);
  reply.write_payload_u32(output->scale.denominator);
  reply.write_payload_u16(static_cast<std::uint16_t>(output->transform));
  const std::array flags{static_cast<std::uint8_t>(output->primary),
                         static_cast<std::uint8_t>(output->enabled)};
  reply.write_payload(flags);
  reply.write_payload_u32(static_cast<std::uint32_t>(generation >> 32U));
  reply.write_payload_u32(static_cast<std::uint32_t>(generation));
  return {std::move(reply).finish()};
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

  auto scale = window->scale;
  const bool invalidated = invalidate_scaled_pixmap(scale);
  scale.accepted_buffer_scale = requested_scale;
  scale.presentation =
      WindowScalePresentationState::ScaleAwareAwaitingPixmap;
  scale.scaled_pixmap_storage.reset();
  scale.presentation_serial = 0;
  const auto preferred_numerator = scale.has_output_state
                                       ? scale.preferred_scale_numerator
                                       : UINT32_C(1);
  const auto preferred_denominator =
      scale.has_output_state
          ? scale.preferred_scale_denominator
          : UINT32_C(1);
  const auto generation = scale.has_output_state
                              ? scale.layout_generation
                              : std::uint64_t{kRandRConfigurationTimestamp};
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u32(requested_scale);
  reply.write_u32(preferred_numerator);
  reply.write_u32(preferred_denominator);
  reply.write_u32(static_cast<std::uint32_t>(generation >> 32U));
  reply.write_u32(static_cast<std::uint32_t>(generation));
  reply.write_padding(4);
  auto result = context.integrated_lifecycle
                    ? DispatchResult::deferred_scale_change({xid, scale})
                    : DispatchResult{};
  result.output = std::move(reply).finish();
  if (!context.integrated_lifecycle) {
    window->scale = std::move(scale);
    if (invalidated)
      append_gw_scale_notifications(result, *window, xid,
                                    kGwScaleInvalidatedReason);
  }
  return result;
}

bool bounded_damage(const geometry::Rectangle rectangle,
                    const std::uint32_t width,
                    const std::uint32_t height) noexcept {
  if (rectangle.empty() || rectangle.x < 0 || rectangle.y < 0) return false;
  const auto right = static_cast<std::uint64_t>(rectangle.x) + rectangle.width;
  const auto bottom =
      static_cast<std::uint64_t>(rectangle.y) + rectangle.height;
  return right <= width && bottom <= height;
}

geometry::Rectangle logical_damage(const geometry::Rectangle pixels,
                                   const std::uint32_t scale) noexcept {
  const auto right = static_cast<std::uint32_t>(pixels.x) + pixels.width;
  const auto bottom = static_cast<std::uint32_t>(pixels.y) + pixels.height;
  const auto x = static_cast<std::uint32_t>(pixels.x) / scale;
  const auto y = static_cast<std::uint32_t>(pixels.y) / scale;
  const auto logical_right = (right + scale - 1U) / scale;
  const auto logical_bottom = (bottom + scale - 1U) / scale;
  return {static_cast<std::int32_t>(x), static_cast<std::int32_t>(y),
          logical_right - x, logical_bottom - y};
}

DispatchResult present_scaled_pixmap(
    ServerState& state, const DispatchContext& context,
    const x11::FramedRequest& request) {
  if (request.core_size() != 20)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{}, pixmap_xid{}, damage_xid{}, serial{};
  (void)reader.read_u32(xid);
  (void)reader.read_u32(pixmap_xid);
  (void)reader.read_u32(damage_xid);
  (void)reader.read_u32(serial);
  DispatchResult failure;
  auto* window = eligible_window(state, context, request, xid, true, failure);
  if (!window) return failure;
  if (window->scale.presentation == WindowScalePresentationState::Legacy ||
      window->scale.accepted_buffer_scale == 0)
    return extension_error(context, request, 1, xid);
  if (std::ranges::any_of(window->children, [&](const std::uint32_t child_xid) {
        const auto* child = state.resources().find_window(child_xid);
        return child && child->window_class == WindowClass::InputOutput &&
               child->map_requested;
      }))
    return extension_error(context, request, 1, xid);
  const auto* pixmap = state.resources().find_pixmap(pixmap_xid);
  if (!pixmap)
    return error(context, request, x11::CoreErrorCode::BadPixmap, pixmap_xid);
  const auto* pixels = pixmap->pixels();
  const auto scale = window->scale.accepted_buffer_scale;
  const auto expected_width =
      static_cast<std::uint64_t>(window->width) * scale;
  const auto expected_height =
      static_cast<std::uint64_t>(window->height) * scale;
  if (pixmap->depth != 24 || !pixels || pixmap->width != expected_width ||
      pixmap->height != expected_height)
    return extension_error(context, request, 0, pixmap_xid);

  std::vector<geometry::Rectangle> buffer_damage;
  if (damage_xid == 0) {
    buffer_damage.push_back({0, 0, pixmap->width, pixmap->height});
  } else {
    const auto* region = state.resources().find_xfixes_region(damage_xid);
    if (!region) return xfixes_region_error(context, request, damage_xid);
    if (!std::ranges::all_of(region->rectangles, [&](const auto rectangle) {
          return bounded_damage(rectangle, pixmap->width, pixmap->height);
        }))
      return extension_error(context, request, 0, damage_xid);
    buffer_damage = region->rectangles;
  }

  auto next = window->scale;
  next.presentation = WindowScalePresentationState::ScaleAwareActive;
  next.scaled_pixmap_storage =
      std::get<std::shared_ptr<PixelStorage>>(pixmap->storage);
  next.presentation_serial = serial;
  const auto generation = next.has_output_state
                              ? next.layout_generation
                              : std::uint64_t{kRandRConfigurationTimestamp};
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u32(serial);
  reply.write_u32(scale);
  reply.write_u32(static_cast<std::uint32_t>(generation >> 32U));
  reply.write_u32(static_cast<std::uint32_t>(generation));
  reply.write_padding(8);
  auto result = context.integrated_lifecycle
                    ? DispatchResult::deferred_scale_change({xid, next})
                    : DispatchResult{};
  result.output = std::move(reply).finish();
  if (!context.integrated_lifecycle) window->scale = std::move(next);
  result.drawable_damage.reserve(buffer_damage.size());
  for (const auto rectangle : buffer_damage)
    result.drawable_damage.push_back(
        {xid, logical_damage(rectangle, scale), rectangle});
  return result;
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
  auto scale = window->scale;
  const bool invalidated = invalidate_scaled_pixmap(scale);
  scale.accepted_buffer_scale = 1;
  scale.presentation = WindowScalePresentationState::Legacy;
  scale.scaled_pixmap_storage.reset();
  scale.presentation_serial = 0;
  auto result = context.integrated_lifecycle
                    ? DispatchResult::deferred_scale_change({xid, scale})
                    : DispatchResult{};
  if (!context.integrated_lifecycle) {
    window->scale = std::move(scale);
    if (invalidated)
      append_gw_scale_notifications(result, *window, xid,
                                    kGwScaleInvalidatedReason);
  }
  return result;
}

}  // namespace

DispatchResult dispatch_gw_scale(ServerState& state,
                                 const DispatchContext& context,
                                 const x11::FramedRequest& request) {
  switch (request.data) {
    case 0: return query_version(context, request);
    case 1: return select_input(state, context, request);
    case 2: return get_output_scale(state, context, request);
    case 3: return get_window_scale(state, context, request);
    case 4: return set_window_buffer_scale(state, context, request);
    case 5: return present_scaled_pixmap(state, context, request);
    case 6: return reset_window_buffer_scale(state, context, request);
    default:
      return error(context, request, x11::CoreErrorCode::BadRequest);
  }
}

}  // namespace glasswyrm::server::extensions
