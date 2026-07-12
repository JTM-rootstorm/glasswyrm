#include "glasswyrmd/request_dispatcher.hpp"

#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/core.hpp"
#include "protocol/x11/reply.hpp"
#include "protocol/x11/lifecycle_request.hpp"
#include "glasswyrmd/raster_ops.hpp"
#include "core/geometry/region.hpp"
#include "protocol/x11/exposure_event.hpp"

#include <bit>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <tuple>
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

std::optional<StructuralEventState> capture_structural_state(
    const ResourceTable& resources, const std::uint32_t target) {
  constexpr std::uint32_t kStructureNotifyMask = 1U << 17U;
  constexpr std::uint32_t kSubstructureNotifyMask = 1U << 19U;
  const auto* window = resources.find_window(target);
  if (!window) return std::nullopt;
  const auto* parent = resources.find_window(window->parent);
  StructuralEventState state{};
  state.target = target;
  state.parent = window->parent;
  state.x = window->x;
  state.y = window->y;
  state.width = window->width;
  state.height = window->height;
  state.border_width = window->border_width;
  state.override_redirect = window->attributes.override_redirect;
  state.mapped = window->map_requested;
  state.viewable = window->map_state == MapState::Viewable;
  if (parent) {
    const auto position = std::ranges::find(parent->children, target);
    if (position != parent->children.end() &&
        std::next(position) != parent->children.end())
      state.above_sibling = *std::next(position);
  }
  for (const auto& [client, mask] : window->event_selections)
    if ((mask & kStructureNotifyMask) != 0)
      state.structure_recipients.push_back(client);
  if (parent)
    for (const auto& [client, mask] : parent->event_selections)
      if ((mask & kSubstructureNotifyMask) != 0)
        state.substructure_recipients.push_back(client);
  std::sort(state.structure_recipients.begin(),
            state.structure_recipients.end());
  std::sort(state.substructure_recipients.begin(),
            state.substructure_recipients.end());
  return state;
}

constexpr std::uint32_t kWindowAttributeMask = 0x00007fffU;
constexpr std::uint32_t kCoreEventMask = 0x01ffffffU;
constexpr std::uint32_t kDoNotPropagateMask = 0x0000204fU;
constexpr std::uint32_t kButtonPressMask = 1U << 2U;
constexpr std::uint32_t kResizeRedirectMask = 1U << 18U;
constexpr std::uint32_t kSubstructureRedirectMask = 1U << 20U;

bool supported_window_drawable(const ResourceTable& resources,
                               const std::uint32_t xid) {
  const auto* window = resources.find_window(xid);
  return window && xid != resources.screen().root_window &&
         window->parent == resources.screen().root_window &&
         window->window_class == WindowClass::InputOutput && window->depth == 24;
}

bool known_drawable(const ResourceTable& resources, const std::uint32_t xid) {
  return resources.find_window(xid) || resources.find_pixmap(xid);
}

std::vector<geometry::Rectangle> rectangle_difference(
    const geometry::Rectangle rectangle, const geometry::Rectangle cutter) {
  const auto overlap = geometry::intersect(rectangle, cutter);
  if (!overlap) return {rectangle};
  std::vector<geometry::Rectangle> result;
  const auto right = rectangle.x + static_cast<std::int64_t>(rectangle.width);
  const auto bottom = rectangle.y + static_cast<std::int64_t>(rectangle.height);
  const auto cut_right = overlap->x + static_cast<std::int64_t>(overlap->width);
  const auto cut_bottom = overlap->y + static_cast<std::int64_t>(overlap->height);
  if (overlap->y > rectangle.y) result.push_back({rectangle.x, rectangle.y, rectangle.width, static_cast<std::uint32_t>(overlap->y-rectangle.y)});
  if (overlap->x > rectangle.x) result.push_back({rectangle.x, overlap->y, static_cast<std::uint32_t>(overlap->x-rectangle.x), overlap->height});
  if (cut_right < right) result.push_back({static_cast<std::int32_t>(cut_right), overlap->y, static_cast<std::uint32_t>(right-cut_right), overlap->height});
  if (cut_bottom < bottom) result.push_back({rectangle.x, static_cast<std::int32_t>(cut_bottom), rectangle.width, static_cast<std::uint32_t>(bottom-cut_bottom)});
  std::sort(result.begin(), result.end(), [](const auto& left, const auto& right_value) {
    return std::tie(left.y,left.x,left.height,left.width) <
           std::tie(right_value.y,right_value.x,right_value.height,right_value.width);
  });
  return result;
}

PixelStorage* mutable_storage(ResourceTable& resources, const std::uint32_t xid) {
  if (auto* pixmap = resources.find_pixmap(xid)) return pixmap->storage.get();
  if (!supported_window_drawable(resources, xid)) return nullptr;
  auto* window = resources.find_window(xid);
  if (!window->storage) {
    auto storage = PixelStorage::create(window->width, window->height);
    if (!storage) return nullptr;
    window->storage = std::make_shared<PixelStorage>(std::move(*storage));
    if (window->attributes.background_source == BackgroundSource::Pixel)
      window->storage->fill({0, 0, window->width, window->height},
                            window->attributes.background_pixel);
  }
  return window->storage.get();
}

struct GcDecodeResult { bool success{}; x11::CoreErrorCode error{x11::CoreErrorCode::BadImplementation}; std::uint32_t bad{}; GraphicsContextResource gc; };
GcDecodeResult decode_gc_values(x11::ByteReader& reader, std::uint32_t mask,
                                GraphicsContextResource gc) {
  constexpr std::uint32_t supported = (1U << 0U) | (1U << 1U) | (1U << 2U) |
      (1U << 3U) | (1U << 8U) | (1U << 15U) | (1U << 16U) | (1U << 17U) |
      (1U << 18U) | (1U << 19U);
  GcDecodeResult result{}; result.gc = gc;
  if ((mask & ~supported) != 0) return result;
  for (std::uint32_t bit = 0; bit < 23; ++bit) {
    if ((mask & (1U << bit)) == 0) continue;
    std::uint32_t value{}; if (!reader.read_u32(value)) return result; result.bad = value;
    switch (bit) {
      case 0: if (value != 3) { result.error=x11::CoreErrorCode::BadValue; return result; } result.gc.function=3; break;
      case 1: result.gc.plane_mask=value; break;
      case 2: result.gc.foreground=value & 0x00ffffffU; break;
      case 3: result.gc.background=value & 0x00ffffffU; break;
      case 8: if (value != 0) { result.error=x11::CoreErrorCode::BadValue; return result; } result.gc.fill_style=0; break;
      case 15: if (value != 0) { result.error=x11::CoreErrorCode::BadValue; return result; } result.gc.subwindow_mode=0; break;
      case 16: if (value > 1) { result.error=x11::CoreErrorCode::BadValue; return result; } result.gc.graphics_exposures=value != 0; break;
      case 17: result.gc.clip_x_origin=static_cast<std::int16_t>(value); break;
      case 18: result.gc.clip_y_origin=static_cast<std::int16_t>(value); break;
      case 19: if (value != 0) { result.error=x11::CoreErrorCode::BadPixmap; return result; } result.gc.clip_mask=0; break;
      default: break;
    }
  }
  result.success = true; return result;
}

struct DecodedWindowAttributes {
  WindowAttributes attributes;
  std::optional<std::uint32_t> event_mask;
  x11::CoreErrorCode error{x11::CoreErrorCode::BadImplementation};
  std::uint32_t bad_value{0};
  bool success{false};
};

DecodedWindowAttributes decode_window_attributes(
    x11::ByteReader& reader, const std::uint32_t value_mask,
    WindowAttributes attributes, const std::uint32_t default_colormap) {
  DecodedWindowAttributes result;
  result.attributes = attributes;
  for (std::uint32_t bit = 0; bit < 15; ++bit) {
    if ((value_mask & (std::uint32_t{1} << bit)) == 0) continue;
    std::uint32_t value = 0;
    if (!reader.read_u32(value)) return result;
    result.bad_value = value;
    switch (bit) {
      case 0:
        if (value > 1) { result.error = x11::CoreErrorCode::BadPixmap; return result; }
        result.attributes.background_pixmap = value;
        result.attributes.background_source = value == 0
            ? BackgroundSource::None : BackgroundSource::ParentRelative;
        break;
      case 1:
        result.attributes.background_pixel = value & 0x00ffffffU;
        result.attributes.background_source = BackgroundSource::Pixel;
        break;
      case 2:
        if (value != 0) { result.error = x11::CoreErrorCode::BadPixmap; return result; }
        result.attributes.border_pixmap = value;
        break;
      case 3: result.attributes.border_pixel = value; break;
      case 4:
        if (value > 10) { result.error = x11::CoreErrorCode::BadValue; return result; }
        result.attributes.bit_gravity = static_cast<std::uint8_t>(value);
        break;
      case 5:
        if (value > 10) { result.error = x11::CoreErrorCode::BadValue; return result; }
        result.attributes.window_gravity = static_cast<std::uint8_t>(value);
        break;
      case 6:
        if (value > 2) { result.error = x11::CoreErrorCode::BadValue; return result; }
        result.attributes.backing_store = static_cast<std::uint8_t>(value);
        break;
      case 7: result.attributes.backing_planes = value; break;
      case 8: result.attributes.backing_pixel = value; break;
      case 9:
        if (value > 1) { result.error = x11::CoreErrorCode::BadValue; return result; }
        result.attributes.override_redirect = value != 0;
        break;
      case 10:
        if (value > 1) { result.error = x11::CoreErrorCode::BadValue; return result; }
        result.attributes.save_under = value != 0;
        break;
      case 11:
        if ((value & ~kCoreEventMask) != 0) { result.error = x11::CoreErrorCode::BadValue; return result; }
        result.event_mask = value;
        break;
      case 12:
        if ((value & ~kDoNotPropagateMask) != 0) { result.error = x11::CoreErrorCode::BadValue; return result; }
        result.attributes.do_not_propagate_mask = value;
        break;
      case 13:
        if (value != 0 && value != default_colormap) { result.error = x11::CoreErrorCode::BadColormap; return result; }
        result.attributes.colormap = value;
        break;
      case 14:
        if (value != 0) { result.error = x11::CoreErrorCode::BadCursor; return result; }
        result.attributes.cursor = value;
        break;
      default: break;
    }
  }
  result.success = true;
  return result;
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
  if ((value_mask & ~kWindowAttributeMask) != 0) {
    return error(context, request, x11::CoreErrorCode::BadValue, value_mask);
  }
  const std::size_t value_count =
      static_cast<std::size_t>(std::popcount(value_mask));
  if (value_count > (std::numeric_limits<std::size_t>::max() - 32) / 4 ||
      !exact_size(request, 32 + value_count * 4)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }

  auto decoded = decode_window_attributes(reader, value_mask, spec.attributes,
                                          state.screen().default_colormap);
  if (!decoded.success) return error(context, request, decoded.error, decoded.bad_value);
  spec.attributes = decoded.attributes;
  spec.initial_event_mask = decoded.event_mask.value_or(0);

  const auto* parent = state.resources().find_window(spec.parent);
  if (context.integrated_lifecycle &&
      state.resources().cleanup_pending(spec.parent))
    return error(context, request, x11::CoreErrorCode::BadWindow, spec.parent);
  const bool policy_candidate =
      parent != nullptr && spec.parent == state.screen().root_window &&
      (spec.window_class == WindowClass::InputOutput ||
       (spec.window_class == WindowClass::CopyFromParent &&
        parent->window_class == WindowClass::InputOutput));
  if (context.integrated_lifecycle && policy_candidate) {
    auto staged = state;
    const auto status = staged.resources().create_window(
        context.client_id, context.resource_base, context.resource_mask, spec);
    switch (status) {
      case CreateWindowStatus::Success:
        return DispatchResult::deferred_create_window(std::move(spec));
      case CreateWindowStatus::BadIdChoice:
        return error(context, request, x11::CoreErrorCode::BadIDChoice,
                     spec.xid);
      case CreateWindowStatus::BadWindow:
        return error(context, request, x11::CoreErrorCode::BadWindow,
                     spec.parent);
      case CreateWindowStatus::BadValue:
        return error(context, request, x11::CoreErrorCode::BadValue);
      case CreateWindowStatus::BadMatch:
        return error(context, request, x11::CoreErrorCode::BadMatch);
      case CreateWindowStatus::BadAlloc:
        return error(context, request, x11::CoreErrorCode::BadAlloc);
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
  if (context.integrated_lifecycle && state.resources().cleanup_pending(window))
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  if (window == state.screen().root_window) return {};
  if (context.integrated_lifecycle &&
      state.resources().is_policy_candidate(window)) {
    return DispatchResult::deferred_destroy_window(window);
  }
  const auto destroyed = state.resources().capture_destroy_plan(window);
  if (!destroyed)
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  DispatchResult result;
  result.structural_transitions.reserve(destroyed->postorder.size());
  for (const auto& item : destroyed->postorder) {
    StructuralEventState before{};
    before.target = item.xid;
    before.parent = item.parent;
    before.structure_recipients = item.structure_recipients;
    before.substructure_recipients = item.substructure_recipients;
    result.structural_transitions.push_back(
        {StructuralTransitionKind::Destroy, std::move(before), std::nullopt});
  }
  const auto status = state.resources().commit_destroy_plan(*destroyed);
  if (status == DestroyWindowStatus::BadWindow) {
    return error(context, request, x11::CoreErrorCode::BadWindow, window);
  }
  return result;
}

DispatchResult change_window_attributes(
    ServerState& state, const DispatchContext& context,
    const x11::FramedRequest& request) {
  if (request.bytes.size() < 12) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t window_id = 0;
  std::uint32_t value_mask = 0;
  if (!reader.read_u32(window_id) || !reader.read_u32(value_mask)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  if ((value_mask & ~kWindowAttributeMask) != 0) {
    return error(context, request, x11::CoreErrorCode::BadValue, value_mask);
  }
  const auto value_count = static_cast<std::size_t>(std::popcount(value_mask));
  if (!exact_size(request, 12 + value_count * 4)) {
    return error(context, request, x11::CoreErrorCode::BadLength);
  }
  auto* window = state.resources().find_window(window_id);
  if (window == nullptr) {
    return error(context, request, x11::CoreErrorCode::BadWindow, window_id);
  }
  if (context.integrated_lifecycle &&
      state.resources().cleanup_pending(window_id))
    return error(context, request, x11::CoreErrorCode::BadWindow, window_id);
  auto decoded = decode_window_attributes(reader, value_mask,
                                          window->attributes,
                                          state.screen().default_colormap);
  if (!decoded.success) {
    return error(context, request, decoded.error, decoded.bad_value);
  }
  if ((value_mask & (1U << 9U)) != 0 &&
      window_id == state.screen().root_window) {
    return error(context, request, x11::CoreErrorCode::BadMatch, window_id);
  }
  const bool defer_override =
      context.integrated_lifecycle && (value_mask & (1U << 9U)) != 0 &&
      state.resources().is_policy_candidate(window_id) &&
      window->map_requested &&
      decoded.attributes.override_redirect !=
          window->attributes.override_redirect;
  const bool proposed_override = decoded.attributes.override_redirect;
  if (decoded.event_mask.has_value()) {
    const auto selected = *decoded.event_mask;
    if ((selected & (kResizeRedirectMask | kSubstructureRedirectMask)) != 0) {
      return error(context, request, x11::CoreErrorCode::BadAccess, selected);
    }
    if ((selected & kButtonPressMask) != 0) {
      for (const auto& [client, mask] : window->event_selections) {
        if (client != context.client_id && (mask & kButtonPressMask) != 0) {
          return error(context, request, x11::CoreErrorCode::BadAccess,
                       selected);
        }
      }
    }
    // Updating the selection first preserves atomicity if insertion allocates.
    if (!state.resources().set_event_selection(window_id, context.client_id,
                                               selected)) {
      return error(context, request, x11::CoreErrorCode::BadWindow, window_id);
    }
  }
  if (defer_override)
    decoded.attributes.override_redirect = window->attributes.override_redirect;
  window->attributes = decoded.attributes;
  if (defer_override)
    return DispatchResult::deferred_override_change(window_id,
                                                    proposed_override);
  return {};
}

DispatchResult get_window_attributes(ServerState& state,
                                     const DispatchContext& context,
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
  x11::ReplyBuilder reply(context.byte_order, context.sequence,
                          window->attributes.backing_store);
  reply.write_u32(window->visual);
  reply.write_u16(static_cast<std::uint16_t>(window->window_class));
  reply.write_u8(window->attributes.bit_gravity);
  reply.write_u8(window->attributes.window_gravity);
  reply.write_u32(window->attributes.backing_planes);
  reply.write_u32(window->attributes.backing_pixel);
  reply.write_u8(window->attributes.save_under ? 1 : 0);
  reply.write_u8(window->attributes.colormap == state.screen().default_colormap
                     ? 1
                     : 0);
  reply.write_u8(static_cast<std::uint8_t>(window->map_state));
  reply.write_u8(window->attributes.override_redirect ? 1 : 0);
  reply.write_u32(window->attributes.colormap);
  reply.write_payload_u32(state.resources().all_event_selections(window_id));
  reply.write_payload_u32(
      state.resources().event_selection(window_id, context.client_id));
  reply.write_payload_u16(static_cast<std::uint16_t>(
      window->attributes.do_not_propagate_mask));
  reply.write_payload_u16(0);
  return {std::move(reply).finish()};
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
  const auto* pixmap = state.resources().find_pixmap(drawable);
  if (window == nullptr && pixmap == nullptr) {
    return error(context, request, x11::CoreErrorCode::BadDrawable, drawable);
  }
  x11::ReplyBuilder reply(context.byte_order, context.sequence,
                          window ? window->depth : pixmap->depth);
  reply.write_u32(state.screen().root_window);
  reply.write_u16(window ? static_cast<std::uint16_t>(window->x) : 0);
  reply.write_u16(window ? static_cast<std::uint16_t>(window->y) : 0);
  reply.write_u16(window ? window->width : pixmap->width);
  reply.write_u16(window ? window->height : pixmap->height);
  reply.write_u16(window ? window->border_width : 0);
  reply.write_padding(2);
  return {std::move(reply).finish()};
}

DispatchResult create_pixmap(ServerState& state, const DispatchContext& context,
                             const x11::FramedRequest& request) {
  if (!exact_size(request, 16)) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{}, drawable{}; std::uint16_t width{}, height{};
  (void)reader.read_u32(xid); (void)reader.read_u32(drawable);
  (void)reader.read_u16(width); (void)reader.read_u16(height);
  switch (state.resources().create_pixmap(context.client_id, context.resource_base,
      context.resource_mask, xid, drawable, request.data, width, height)) {
    case CreatePixmapStatus::Success: return {};
    case CreatePixmapStatus::BadIdChoice: return error(context, request, x11::CoreErrorCode::BadIDChoice, xid);
    case CreatePixmapStatus::BadDrawable: return error(context, request, x11::CoreErrorCode::BadDrawable, drawable);
    case CreatePixmapStatus::BadValue: return error(context, request, x11::CoreErrorCode::BadValue, request.data == 24 && width && height ? 0 : request.data);
    case CreatePixmapStatus::BadAlloc: return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return {};
}

DispatchResult free_pixmap(ServerState& state, const DispatchContext& context,
                           const x11::FramedRequest& request) {
  if (!exact_size(request, 8)) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order); std::uint32_t xid{}; (void)reader.read_u32(xid);
  return state.resources().free_pixmap(xid) == FreePixmapStatus::Success
      ? DispatchResult{} : error(context, request, x11::CoreErrorCode::BadPixmap, xid);
}

DispatchResult create_gc(ServerState& state, const DispatchContext& context,
                         const x11::FramedRequest& request) {
  if (request.bytes.size() < 16) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order); std::uint32_t xid{}, drawable{}, mask{};
  (void)reader.read_u32(xid); (void)reader.read_u32(drawable); (void)reader.read_u32(mask);
  if (!exact_size(request, 16 + static_cast<std::size_t>(std::popcount(mask)) * 4U))
    return error(context, request, x11::CoreErrorCode::BadLength);
  const auto decoded = decode_gc_values(reader, mask, {});
  if (!decoded.success) return error(context, request, decoded.error, decoded.bad);
  switch (state.resources().create_gc(context.client_id, context.resource_base,
      context.resource_mask, xid, drawable, decoded.gc)) {
    case CreateGcStatus::Success: return {};
    case CreateGcStatus::BadIdChoice: return error(context, request, x11::CoreErrorCode::BadIDChoice, xid);
    case CreateGcStatus::BadDrawable: return error(context, request, x11::CoreErrorCode::BadDrawable, drawable);
    case CreateGcStatus::BadMatch: return error(context, request, x11::CoreErrorCode::BadMatch, drawable);
    case CreateGcStatus::BadAlloc: return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return {};
}

DispatchResult change_gc(ServerState& state, const DispatchContext& context,
                         const x11::FramedRequest& request) {
  if (request.bytes.size() < 12) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order); std::uint32_t xid{}, mask{};
  (void)reader.read_u32(xid); (void)reader.read_u32(mask);
  if (!exact_size(request, 12 + static_cast<std::size_t>(std::popcount(mask)) * 4U))
    return error(context, request, x11::CoreErrorCode::BadLength);
  auto* gc = state.resources().find_gc(xid);
  if (!gc) return error(context, request, x11::CoreErrorCode::BadGContext, xid);
  const auto decoded = decode_gc_values(reader, mask, *gc);
  if (!decoded.success) return error(context, request, decoded.error, decoded.bad);
  *gc = decoded.gc; return {};
}

DispatchResult free_gc(ServerState& state, const DispatchContext& context,
                       const x11::FramedRequest& request) {
  if (!exact_size(request, 8)) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order); std::uint32_t xid{}; (void)reader.read_u32(xid);
  return state.resources().free_gc(xid) == FreeGcStatus::Success
      ? DispatchResult{} : error(context, request, x11::CoreErrorCode::BadGContext, xid);
}

DispatchResult put_image(ServerState& state, const DispatchContext& context,
                         const x11::FramedRequest& request) {
  if (request.bytes.size() < 24) return error(context, request, x11::CoreErrorCode::BadLength);
  if (request.data > 2) return error(context, request, x11::CoreErrorCode::BadValue, request.data);
  if (request.data != 2) return error(context, request, x11::CoreErrorCode::BadImplementation);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t drawable{}, gc_id{}; std::uint16_t width{}, height{}, raw_x{}, raw_y{}; std::uint8_t left_pad{}, depth{};
  (void)reader.read_u32(drawable); (void)reader.read_u32(gc_id); (void)reader.read_u16(width);
  (void)reader.read_u16(height); (void)reader.read_u16(raw_x); (void)reader.read_u16(raw_y);
  (void)reader.read_u8(left_pad); (void)reader.read_u8(depth); (void)reader.skip(2);
  const auto payload_size = static_cast<std::uint64_t>(width) * height * 4U;
  if (payload_size > std::numeric_limits<std::size_t>::max() ||
      request.bytes.size() != 24U + payload_size)
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (depth != 24 || left_pad != 0) return error(context, request, x11::CoreErrorCode::BadValue, depth != 24 ? depth : left_pad);
  auto* gc = state.resources().find_gc(gc_id);
  if (!gc) return error(context, request, x11::CoreErrorCode::BadGContext, gc_id);
  const bool valid = state.resources().find_pixmap(drawable) || supported_window_drawable(state.resources(), drawable);
  if (!valid) return error(context, request, known_drawable(state.resources(), drawable)
      ? x11::CoreErrorCode::BadMatch : x11::CoreErrorCode::BadDrawable, drawable);
  auto* storage = mutable_storage(state.resources(), drawable);
  if (!storage) return error(context, request, x11::CoreErrorCode::BadAlloc);
  const auto payload = std::span<const std::uint8_t>(request.bytes).subspan(24);
  const auto raster = put_zpixmap(*storage, static_cast<std::int16_t>(raw_x),
      static_cast<std::int16_t>(raw_y), width, height, payload, gc->plane_mask);
  if (!raster.success) return error(context, request, x11::CoreErrorCode::BadLength);
  DispatchResult result;
  if (!raster.damage.empty() && supported_window_drawable(state.resources(), drawable))
    result.drawable_damage.push_back({drawable, raster.damage});
  return result;
}

DispatchResult poly_fill_rectangle(ServerState& state, const DispatchContext& context,
                                   const x11::FramedRequest& request) {
  if (request.bytes.size() < 12 || (request.bytes.size() - 12U) % 8U != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order); std::uint32_t drawable{}, gc_id{};
  (void)reader.read_u32(drawable); (void)reader.read_u32(gc_id);
  auto* gc = state.resources().find_gc(gc_id);
  if (!gc) return error(context, request, x11::CoreErrorCode::BadGContext, gc_id);
  const bool valid = state.resources().find_pixmap(drawable) || supported_window_drawable(state.resources(), drawable);
  if (!valid) return error(context, request, x11::CoreErrorCode::BadDrawable, drawable);
  struct Fill { geometry::Rectangle rectangle; }; std::vector<Fill> fills;
  fills.reserve((request.bytes.size() - 12U) / 8U);
  while (reader.remaining() != 0) { std::uint16_t x{}, y{}, w{}, h{}; (void)reader.read_u16(x); (void)reader.read_u16(y); (void)reader.read_u16(w); (void)reader.read_u16(h); fills.push_back({{static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), w, h}}); }
  auto* storage = mutable_storage(state.resources(), drawable);
  if (!storage) return error(context, request, x11::CoreErrorCode::BadAlloc);
  DispatchResult result;
  geometry::Region damage({0, 0, storage->width(), storage->height()});
  for (const auto& fill : fills) damage.add(fill.rectangle);
  if (supported_window_drawable(state.resources(), drawable))
    result.drawable_damage.reserve(damage.rectangles().size());
  for (const auto& rectangle : damage.rectangles()) {
    storage->fill(rectangle, gc->foreground, gc->plane_mask);
    if (supported_window_drawable(state.resources(), drawable))
      result.drawable_damage.push_back({drawable, rectangle});
  }
  return result;
}

DispatchResult copy_area_request(ServerState& state, const DispatchContext& context,
                                 const x11::FramedRequest& request) {
  if (!exact_size(request, 28)) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order); std::uint32_t source{}, destination{}, gc_id{};
  std::uint16_t sx{}, sy{}, dx{}, dy{}, width{}, height{};
  (void)reader.read_u32(source); (void)reader.read_u32(destination); (void)reader.read_u32(gc_id);
  (void)reader.read_u16(sx); (void)reader.read_u16(sy); (void)reader.read_u16(dx); (void)reader.read_u16(dy); (void)reader.read_u16(width); (void)reader.read_u16(height);
  auto* gc = state.resources().find_gc(gc_id);
  if (!gc) return error(context, request, x11::CoreErrorCode::BadGContext, gc_id);
  const bool valid_source = state.resources().find_pixmap(source) || supported_window_drawable(state.resources(), source);
  const bool valid_destination = state.resources().find_pixmap(destination) || supported_window_drawable(state.resources(), destination);
  if (!valid_source || !valid_destination) {
    const auto bad = !valid_source ? source : destination;
    return error(context, request, known_drawable(state.resources(), bad)
        ? x11::CoreErrorCode::BadMatch : x11::CoreErrorCode::BadDrawable, bad);
  }
  auto* source_storage = mutable_storage(state.resources(), source); auto* destination_storage = mutable_storage(state.resources(), destination);
  if (!source_storage || !destination_storage) return error(context, request, x11::CoreErrorCode::BadAlloc);
  RasterResult raster;
  try { raster = copy_area(*source_storage, *destination_storage, static_cast<std::int16_t>(sx), static_cast<std::int16_t>(sy), width, height, static_cast<std::int16_t>(dx), static_cast<std::int16_t>(dy), gc->plane_mask); }
  catch (const std::bad_alloc&) { return error(context, request, x11::CoreErrorCode::BadAlloc); }
  DispatchResult result;
  if (!raster.damage.empty() && supported_window_drawable(state.resources(), destination)) result.drawable_damage.push_back({destination, raster.damage});
  if (gc->graphics_exposures) {
    const auto requested = geometry::intersect({static_cast<std::int16_t>(dx), static_cast<std::int16_t>(dy), width, height}, {0, 0, destination_storage->width(), destination_storage->height()});
    if (requested && raster.damage == *requested)
      result.output = x11::encode_no_expose(context.byte_order, context.sequence, {destination, 0, request.opcode});
    else if (requested) {
      auto missing = raster.damage.empty() ? std::vector<geometry::Rectangle>{*requested}
                                           : rectangle_difference(*requested, raster.damage);
      result.output.reserve(missing.size() * 32U);
      for (std::size_t index = 0; index < missing.size(); ++index) {
        const auto& rectangle = missing[index];
        auto event = x11::encode_graphics_expose(context.byte_order, context.sequence,
            {destination, static_cast<std::uint16_t>(rectangle.x), static_cast<std::uint16_t>(rectangle.y),
             static_cast<std::uint16_t>(rectangle.width), static_cast<std::uint16_t>(rectangle.height), 0,
             static_cast<std::uint16_t>(missing.size()-index-1), request.opcode});
        result.output.insert(result.output.end(), event.begin(), event.end());
      }
    }
  }
  return result;
}

DispatchResult clear_area(ServerState& state, const DispatchContext& context,
                          const x11::FramedRequest& request) {
  if (!exact_size(request, 16)) return error(context, request, x11::CoreErrorCode::BadLength);
  if (request.data > 1) return error(context, request, x11::CoreErrorCode::BadValue, request.data);
  x11::ByteReader reader(request.body(), context.byte_order); std::uint32_t window_id{}; std::uint16_t x{}, y{}, width{}, height{};
  (void)reader.read_u32(window_id); (void)reader.read_u16(x); (void)reader.read_u16(y); (void)reader.read_u16(width); (void)reader.read_u16(height);
  auto* window = state.resources().find_window(window_id);
  if (!window) return error(context, request, x11::CoreErrorCode::BadWindow, window_id);
  if (!supported_window_drawable(state.resources(), window_id)) return error(context, request, x11::CoreErrorCode::BadMatch, window_id);
  auto* storage = mutable_storage(state.resources(), window_id); if (!storage) return error(context, request, x11::CoreErrorCode::BadAlloc);
  const auto signed_x = static_cast<std::int16_t>(x), signed_y = static_cast<std::int16_t>(y);
  const auto effective_width = width ? width : (signed_x < static_cast<std::int32_t>(window->width) ? window->width - signed_x : 0U);
  const auto effective_height = height ? height : (signed_y < static_cast<std::int32_t>(window->height) ? window->height - signed_y : 0U);
  const auto clipped = geometry::intersect({signed_x, signed_y, effective_width, effective_height}, {0, 0, window->width, window->height});
  DispatchResult result; if (!clipped) return result;
  if (window->attributes.background_source != BackgroundSource::None) {
    storage->fill(*clipped, window->attributes.background_source == BackgroundSource::Pixel ? window->attributes.background_pixel : 0);
    result.drawable_damage.push_back({window_id, *clipped});
  }
  if (request.data == 1) result.expose_intents.push_back({window_id, *clipped});
  return result;
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
  reply.write_u32(state.focused_window());
  return {std::move(reply).finish()};
}

DispatchResult lifecycle_decode_error(const DispatchContext& context,
                                      const x11::FramedRequest& request,
                                      x11::LifecycleDecodeStatus status) {
  if (status == x11::LifecycleDecodeStatus::BadValue)
    return error(context, request, x11::CoreErrorCode::BadValue);
  if (status == x11::LifecycleDecodeStatus::BadMatch)
    return error(context, request, x11::CoreErrorCode::BadMatch);
  return error(context, request, x11::CoreErrorCode::BadLength);
}

DispatchResult map_window(ServerState& state, const DispatchContext& context,
                          const x11::FramedRequest& request, bool mapped) {
  x11::WindowLifecycleRequest decoded;
  const auto status = mapped
      ? x11::decode_map_window(request.bytes, context.byte_order, decoded)
      : x11::decode_unmap_window(request.bytes, context.byte_order, decoded);
  if (status != x11::LifecycleDecodeStatus::Complete)
    return lifecycle_decode_error(context, request, status);
  if (!state.resources().find_window(decoded.window))
    return error(context, request, x11::CoreErrorCode::BadWindow, decoded.window);
  if (context.integrated_lifecycle &&
      state.resources().cleanup_pending(decoded.window))
    return error(context, request, x11::CoreErrorCode::BadWindow, decoded.window);
  if (decoded.window == state.screen().root_window) return {};
  if (state.resources().is_policy_candidate(decoded.window) &&
      context.integrated_lifecycle)
    return DispatchResult::deferred(decoded.window, {}, mapped);
  if (state.resources().is_policy_candidate(decoded.window))
    return error(context, request, x11::CoreErrorCode::BadImplementation);
  const auto before = capture_structural_state(state.resources(), decoded.window);
  switch (state.resources().set_local_map_intent(decoded.window, mapped)) {
    case LocalLifecycleStatus::Success: {
      DispatchResult result;
      result.structural_transitions.push_back(
          {mapped ? StructuralTransitionKind::Map
                  : StructuralTransitionKind::Unmap,
           before, capture_structural_state(state.resources(), decoded.window)});
      return result;
    }
    case LocalLifecycleStatus::BadWindow:
      return error(context, request, x11::CoreErrorCode::BadWindow, decoded.window);
    case LocalLifecycleStatus::BadMatch:
      return error(context, request, x11::CoreErrorCode::BadMatch, decoded.window);
    case LocalLifecycleStatus::BadValue:
      return error(context, request, x11::CoreErrorCode::BadValue);
  }
  return error(context, request, x11::CoreErrorCode::BadImplementation);
}

DispatchResult configure_window(ServerState& state,
                                const DispatchContext& context,
                                const x11::FramedRequest& request) {
  x11::ConfigureWindowRequest decoded;
  const auto status = x11::decode_configure_window(request.bytes,
                                                    context.byte_order, decoded);
  if (status != x11::LifecycleDecodeStatus::Complete)
    return lifecycle_decode_error(context, request, status);
  if (!state.resources().find_window(decoded.window))
    return error(context, request, x11::CoreErrorCode::BadWindow, decoded.window);
  if (context.integrated_lifecycle &&
      state.resources().cleanup_pending(decoded.window))
    return error(context, request, x11::CoreErrorCode::BadWindow, decoded.window);
  if (decoded.window == state.screen().root_window)
    return error(context, request, x11::CoreErrorCode::BadMatch, decoded.window);
  if (decoded.stack_mode && *decoded.stack_mode != x11::CoreStackMode::Above &&
      *decoded.stack_mode != x11::CoreStackMode::Below)
    return error(context, request, x11::CoreErrorCode::BadImplementation,
                 static_cast<std::uint32_t>(*decoded.stack_mode));
  if (state.resources().is_policy_candidate(decoded.window) &&
      context.integrated_lifecycle)
    return DispatchResult::deferred(decoded.window, decoded);
  if (state.resources().is_policy_candidate(decoded.window))
    return error(context, request, x11::CoreErrorCode::BadImplementation);
  LocalConfigure local{decoded.x, decoded.y, decoded.width, decoded.height,
                       decoded.border_width, decoded.sibling,
                       decoded.stack_mode == x11::CoreStackMode::Above
                           ? LifecycleStackMode::Above
                           : decoded.stack_mode == x11::CoreStackMode::Below
                                 ? LifecycleStackMode::Below
                                 : LifecycleStackMode::None};
  const auto before = capture_structural_state(state.resources(), decoded.window);
  switch (state.resources().configure_local(decoded.window, local)) {
    case LocalLifecycleStatus::Success: {
      DispatchResult result;
      result.structural_transitions.push_back(
          {StructuralTransitionKind::Configure, before,
           capture_structural_state(state.resources(), decoded.window)});
      return result;
    }
    case LocalLifecycleStatus::BadWindow:
      return error(context, request, x11::CoreErrorCode::BadWindow, decoded.window);
    case LocalLifecycleStatus::BadMatch:
      return error(context, request, x11::CoreErrorCode::BadMatch, decoded.window);
    case LocalLifecycleStatus::BadValue:
      return error(context, request, x11::CoreErrorCode::BadValue);
  }
  return error(context, request, x11::CoreErrorCode::BadImplementation);
}

}  // namespace

DispatchResult dispatch_request(ServerState& state,
                                const DispatchContext& context,
                                const x11::FramedRequest& request) {
  try {
    switch (static_cast<x11::CoreOpcode>(request.opcode)) {
      case x11::CoreOpcode::CreateWindow:
        return create_window(state, context, request);
      case x11::CoreOpcode::ChangeWindowAttributes:
        return change_window_attributes(state, context, request);
      case x11::CoreOpcode::MapWindow:
        return map_window(state, context, request, true);
      case x11::CoreOpcode::UnmapWindow:
        return map_window(state, context, request, false);
      case x11::CoreOpcode::ConfigureWindow:
        return configure_window(state, context, request);
      case x11::CoreOpcode::GetWindowAttributes:
        return get_window_attributes(state, context, request);
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
      case x11::CoreOpcode::CreatePixmap:
        return create_pixmap(state, context, request);
      case x11::CoreOpcode::FreePixmap:
        return free_pixmap(state, context, request);
      case x11::CoreOpcode::CreateGC:
        return create_gc(state, context, request);
      case x11::CoreOpcode::ChangeGC:
        return change_gc(state, context, request);
      case x11::CoreOpcode::FreeGC:
        return free_gc(state, context, request);
      case x11::CoreOpcode::ClearArea:
        return clear_area(state, context, request);
      case x11::CoreOpcode::CopyArea:
        return copy_area_request(state, context, request);
      case x11::CoreOpcode::PolyFillRectangle:
        return poly_fill_rectangle(state, context, request);
      case x11::CoreOpcode::PutImage:
        return put_image(state, context, request);
      case x11::CoreOpcode::NoOperation:
        return {};
    }
  } catch (const std::bad_alloc&) {
    return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return error(context, request, x11::CoreErrorCode::BadRequest);
}

}  // namespace glasswyrm::server
