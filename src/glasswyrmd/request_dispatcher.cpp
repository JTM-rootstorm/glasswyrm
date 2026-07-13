#include "glasswyrmd/request_dispatcher.hpp"

#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/core.hpp"
#include "protocol/x11/reply.hpp"
#include "protocol/x11/lifecycle_request.hpp"
#include "glasswyrmd/raster_ops.hpp"
#include "glasswyrmd/m9_raster_ops.hpp"
#include "core/geometry/region.hpp"
#include "protocol/x11/exposure_event.hpp"
#include "protocol/x11/event_mask.hpp"

#include <bit>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string_view>
#include <string>
#include <type_traits>
#include <tuple>
#include <unordered_set>
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

std::size_t padded_size(const std::size_t size) { return (size + 3U) & ~std::size_t{3U}; }

struct Color { std::uint16_t red{}, green{}, blue{}; };

std::optional<Color> parse_color_name(std::span<const std::uint8_t> bytes) {
  std::string name(bytes.begin(), bytes.end());
  if (!name.empty() && name.front() == '#') {
    const auto digits = name.size() - 1;
    if (digits != 3 && digits != 6 && digits != 12) return std::nullopt;
    auto nibble = [](const char value) -> std::optional<std::uint8_t> {
      if (value >= '0' && value <= '9') return value - '0';
      if (value >= 'a' && value <= 'f') return value - 'a' + 10;
      if (value >= 'A' && value <= 'F') return value - 'A' + 10;
      return std::nullopt;
    };
    std::array<std::uint16_t, 3> values{};
    const auto width = digits / 3;
    for (std::size_t component = 0; component < 3; ++component) {
      std::uint16_t value = 0;
      for (std::size_t offset = 0; offset < width; ++offset) {
        const auto parsed = nibble(name[1 + component * width + offset]);
        if (!parsed) return std::nullopt;
        value = static_cast<std::uint16_t>((value << 4U) | *parsed);
      }
      values[component] = width == 1 ? static_cast<std::uint16_t>(value * 0x1111U)
                        : width == 2 ? static_cast<std::uint16_t>(value * 0x0101U)
                                     : value;
    }
    return Color{values[0], values[1], values[2]};
  }
  std::ranges::transform(name, name.begin(), [](const unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  if (name == "black") return Color{0, 0, 0};
  if (name == "white") return Color{0xffff, 0xffff, 0xffff};
  if (name == "red") return Color{0xffff, 0, 0};
  if (name == "green") return Color{0, 0xffff, 0};
  if (name == "blue") return Color{0, 0, 0xffff};
  if (name == "yellow") return Color{0xffff, 0xffff, 0};
  if (name == "cyan") return Color{0, 0xffff, 0xffff};
  if (name == "magenta") return Color{0xffff, 0, 0xffff};
  if (name == "gray" || name == "grey") return Color{0x8080, 0x8080, 0x8080};
  if (name == "light gray" || name == "light grey") return Color{0xd3d3, 0xd3d3, 0xd3d3};
  if (name == "dark gray" || name == "dark grey") return Color{0xa9a9, 0xa9a9, 0xa9a9};
  return std::nullopt;
}

Color quantize_color(const Color color) {
  return {static_cast<std::uint16_t>((color.red >> 8U) * 257U),
          static_cast<std::uint16_t>((color.green >> 8U) * 257U),
          static_cast<std::uint16_t>((color.blue >> 8U) * 257U)};
}
std::uint32_t color_pixel(const Color color) {
  return (static_cast<std::uint32_t>(color.red >> 8U) << 16U) |
         (static_cast<std::uint32_t>(color.green >> 8U) << 8U) |
         (color.blue >> 8U);
}

std::optional<StructuralEventState> capture_structural_state(
    const ResourceTable& resources, const std::uint32_t target) {
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
    if ((mask & x11::event_mask::StructureNotify) != 0)
      state.structure_recipients.push_back(client);
  if (parent)
    for (const auto& [client, mask] : parent->event_selections)
      if ((mask & x11::event_mask::SubstructureNotify) != 0)
        state.substructure_recipients.push_back(client);
  std::sort(state.structure_recipients.begin(),
            state.structure_recipients.end());
  std::sort(state.substructure_recipients.begin(),
            state.substructure_recipients.end());
  return state;
}

constexpr std::uint32_t kWindowAttributeMask = 0x00007fffU;
constexpr auto kCoreEventMask = x11::event_mask::All;
constexpr auto kDoNotPropagateMask = x11::event_mask::DoNotPropagate;
constexpr auto kButtonPressMask = x11::event_mask::ButtonPress;
constexpr auto kResizeRedirectMask = x11::event_mask::ResizeRedirect;
constexpr auto kSubstructureRedirectMask = x11::event_mask::SubstructureRedirect;

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
  if (auto* pixmap = resources.find_pixmap(xid)) return pixmap->pixels();
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
                                GraphicsContextResource gc,
                                const ResourceTable& resources) {
  constexpr std::uint32_t supported = (1U << 0U) | (1U << 1U) | (1U << 2U) |
      (1U << 3U) | (1U << 4U) | (1U << 5U) | (1U << 6U) |
      (1U << 7U) | (1U << 8U) | (1U << 14U) | (1U << 15U) |
      (1U << 16U) | (1U << 17U) |
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
      case 4:
        if (value > std::numeric_limits<std::uint16_t>::max()) {
          result.error=x11::CoreErrorCode::BadValue; return result;
        }
        if (value != 0) return result;
        result.gc.line_width=0; break;
      case 5:
        if (value > 2) { result.error=x11::CoreErrorCode::BadValue; return result; }
        if (value != 0) return result;
        result.gc.line_style=0; break;
      case 6:
        if (value > 3) { result.error=x11::CoreErrorCode::BadValue; return result; }
        if (value != 1) return result;
        result.gc.cap_style=1; break;
      case 7:
        if (value > 2) { result.error=x11::CoreErrorCode::BadValue; return result; }
        if (value != 0) return result;
        result.gc.join_style=0; break;
      case 8: if (value != 0) { result.error=x11::CoreErrorCode::BadValue; return result; } result.gc.fill_style=0; break;
      case 14:
        if (!resources.find_font(value)) {
          result.error = x11::CoreErrorCode::BadFont;
          return result;
        }
        result.gc.font = kDefaultFontXid;
        break;
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
    case CreatePixmapStatus::BadMatch: return error(context, request, x11::CoreErrorCode::BadMatch, drawable);
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
  const auto decoded = decode_gc_values(reader, mask, {}, state.resources());
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
  const auto decoded = decode_gc_values(reader, mask, *gc, state.resources());
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

std::optional<geometry::Rectangle> clipped_bounds(
    const PixelStorage& storage, const std::span<const RasterPoint> points) {
  if (points.empty()) return std::nullopt;
  auto minimum_x = points.front().x, maximum_x = points.front().x;
  auto minimum_y = points.front().y, maximum_y = points.front().y;
  for (const auto point : points) {
    minimum_x = std::min(minimum_x, point.x); maximum_x = std::max(maximum_x, point.x);
    minimum_y = std::min(minimum_y, point.y); maximum_y = std::max(maximum_y, point.y);
  }
  return geometry::intersect(
      {minimum_x, minimum_y,
       static_cast<std::uint32_t>(static_cast<std::int64_t>(maximum_x) - minimum_x + 1),
       static_cast<std::uint32_t>(static_cast<std::int64_t>(maximum_y) - minimum_y + 1)},
      {0, 0, storage.width(), storage.height()});
}

DispatchResult poly_line(ServerState& state, const DispatchContext& context,
                         const x11::FramedRequest& request) {
  if (request.bytes.size() < 12 || (request.bytes.size() - 12U) % 4U != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (request.data > 1)
    return error(context, request, x11::CoreErrorCode::BadValue, request.data);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t drawable{}, gc_id{}; (void)reader.read_u32(drawable); (void)reader.read_u32(gc_id);
  const auto* gc = state.resources().find_gc(gc_id);
  if (!gc) return error(context, request, x11::CoreErrorCode::BadGContext, gc_id);
  if (!state.resources().find_pixmap(drawable) && !supported_window_drawable(state.resources(), drawable))
    return error(context, request, known_drawable(state.resources(), drawable)
        ? x11::CoreErrorCode::BadMatch : x11::CoreErrorCode::BadDrawable, drawable);
  std::vector<RasterPoint> points;
  points.reserve((request.bytes.size() - 12U) / 4U);
  while (reader.remaining() != 0) {
    std::uint16_t raw_x{}, raw_y{}; (void)reader.read_u16(raw_x); (void)reader.read_u16(raw_y);
    RasterPoint point{static_cast<std::int16_t>(raw_x), static_cast<std::int16_t>(raw_y)};
    if (request.data == 1 && !points.empty()) {
      const auto x = static_cast<std::int64_t>(points.back().x) + point.x;
      const auto y = static_cast<std::int64_t>(points.back().y) + point.y;
      if (x < std::numeric_limits<std::int32_t>::min() || x > std::numeric_limits<std::int32_t>::max() ||
          y < std::numeric_limits<std::int32_t>::min() || y > std::numeric_limits<std::int32_t>::max())
        return error(context, request, x11::CoreErrorCode::BadValue);
      point = {static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)};
    }
    points.push_back(point);
  }
  auto* storage = mutable_storage(state.resources(), drawable);
  if (!storage) return error(context, request, x11::CoreErrorCode::BadAlloc);
  if (points.size() == 1) draw_line(*storage, points[0], points[0], gc->foreground, gc->plane_mask);
  else for (std::size_t index = 1; index < points.size(); ++index)
    draw_line(*storage, points[index - 1], points[index], gc->foreground, gc->plane_mask);
  DispatchResult result;
  if (supported_window_drawable(state.resources(), drawable))
    if (const auto damage = clipped_bounds(*storage, points)) result.drawable_damage.push_back({drawable, *damage});
  return result;
}

DispatchResult poly_segment(ServerState& state, const DispatchContext& context,
                            const x11::FramedRequest& request) {
  if (request.bytes.size() < 12 || (request.bytes.size() - 12U) % 8U != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t drawable{}, gc_id{}; (void)reader.read_u32(drawable); (void)reader.read_u32(gc_id);
  const auto* gc = state.resources().find_gc(gc_id);
  if (!gc) return error(context, request, x11::CoreErrorCode::BadGContext, gc_id);
  if (!state.resources().find_pixmap(drawable) && !supported_window_drawable(state.resources(), drawable))
    return error(context, request, known_drawable(state.resources(), drawable)
        ? x11::CoreErrorCode::BadMatch : x11::CoreErrorCode::BadDrawable, drawable);
  std::vector<RasterSegment> segments;
  std::vector<RasterPoint> damage_points;
  while (reader.remaining() != 0) {
    std::uint16_t x1{}, y1{}, x2{}, y2{};
    (void)reader.read_u16(x1); (void)reader.read_u16(y1); (void)reader.read_u16(x2); (void)reader.read_u16(y2);
    RasterSegment segment{{static_cast<std::int16_t>(x1), static_cast<std::int16_t>(y1)},
                          {static_cast<std::int16_t>(x2), static_cast<std::int16_t>(y2)}};
    segments.push_back(segment); damage_points.push_back(segment.first); damage_points.push_back(segment.second);
  }
  auto* storage = mutable_storage(state.resources(), drawable);
  if (!storage) return error(context, request, x11::CoreErrorCode::BadAlloc);
  draw_segments(*storage, segments, gc->foreground, gc->plane_mask);
  DispatchResult result;
  if (supported_window_drawable(state.resources(), drawable))
    if (const auto damage = clipped_bounds(*storage, damage_points)) result.drawable_damage.push_back({drawable, *damage});
  return result;
}

DispatchResult fill_poly(ServerState& state, const DispatchContext& context,
                         const x11::FramedRequest& request) {
  if (request.bytes.size() < 16 || (request.bytes.size() - 16U) % 4U != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t drawable{}, gc_id{}; std::uint8_t shape{}, mode{};
  (void)reader.read_u32(drawable); (void)reader.read_u32(gc_id);
  (void)reader.read_u8(shape); (void)reader.read_u8(mode); (void)reader.skip(2);
  if (shape > 2) return error(context, request, x11::CoreErrorCode::BadValue, shape);
  if (mode > 1) return error(context, request, x11::CoreErrorCode::BadValue, mode);
  if (shape != 2 || mode != 0) return error(context, request, x11::CoreErrorCode::BadImplementation);
  const auto* gc = state.resources().find_gc(gc_id);
  if (!gc) return error(context, request, x11::CoreErrorCode::BadGContext, gc_id);
  if (!state.resources().find_pixmap(drawable) && !supported_window_drawable(state.resources(), drawable))
    return error(context, request, known_drawable(state.resources(), drawable)
        ? x11::CoreErrorCode::BadMatch : x11::CoreErrorCode::BadDrawable, drawable);
  std::vector<RasterPoint> points;
  while (reader.remaining() != 0) {
    std::uint16_t x{}, y{}; (void)reader.read_u16(x); (void)reader.read_u16(y);
    points.push_back({static_cast<std::int16_t>(x), static_cast<std::int16_t>(y)});
  }
  auto* storage = mutable_storage(state.resources(), drawable);
  if (!storage) return error(context, request, x11::CoreErrorCode::BadAlloc);
  fill_convex_polygon(*storage, points, gc->foreground, gc->plane_mask);
  DispatchResult result;
  if (supported_window_drawable(state.resources(), drawable))
    if (const auto damage = clipped_bounds(*storage, points)) result.drawable_damage.push_back({drawable, *damage});
  return result;
}

DispatchResult poly_fill_arc(ServerState& state, const DispatchContext& context,
                             const x11::FramedRequest& request) {
  if (request.bytes.size() < 12 || (request.bytes.size() - 12U) % 12U != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t drawable{}, gc_id{}; (void)reader.read_u32(drawable); (void)reader.read_u32(gc_id);
  const auto* gc = state.resources().find_gc(gc_id);
  if (!gc) return error(context, request, x11::CoreErrorCode::BadGContext, gc_id);
  if (!state.resources().find_pixmap(drawable) && !supported_window_drawable(state.resources(), drawable))
    return error(context, request, known_drawable(state.resources(), drawable)
        ? x11::CoreErrorCode::BadMatch : x11::CoreErrorCode::BadDrawable, drawable);
  std::vector<RasterEllipse> ellipses;
  while (reader.remaining() != 0) {
    std::uint16_t x{}, y{}, width{}, height{}, angle1{}, angle2{};
    (void)reader.read_u16(x); (void)reader.read_u16(y); (void)reader.read_u16(width); (void)reader.read_u16(height);
    (void)reader.read_u16(angle1); (void)reader.read_u16(angle2);
    static_cast<void>(angle1);
    const auto extent = static_cast<std::int16_t>(angle2);
    if (extent != 360 * 64 && extent != -360 * 64)
      return error(context, request, x11::CoreErrorCode::BadImplementation);
    ellipses.push_back({static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), width, height});
  }
  auto* storage = mutable_storage(state.resources(), drawable);
  if (!storage) return error(context, request, x11::CoreErrorCode::BadAlloc);
  geometry::Region damage({0, 0, storage->width(), storage->height()});
  for (const auto ellipse : ellipses) {
    fill_ellipse(*storage, ellipse, gc->foreground, gc->plane_mask);
    damage.add({ellipse.x, ellipse.y, ellipse.width, ellipse.height});
  }
  DispatchResult result;
  if (supported_window_drawable(state.resources(), drawable))
    for (const auto& rectangle : damage.rectangles()) result.drawable_damage.push_back({drawable, rectangle});
  return result;
}

bool valid_fontable(const ResourceTable& resources, const std::uint32_t xid) {
  return resources.find_font(xid) || resources.find_gc(xid);
}

DispatchResult open_font(ServerState& state, const DispatchContext& context,
                         const x11::FramedRequest& request) {
  if (request.bytes.size() < 12) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{}; std::uint16_t name_length{};
  (void)reader.read_u32(xid); (void)reader.read_u16(name_length); (void)reader.skip(2);
  const auto padded = (static_cast<std::size_t>(name_length) + 3U) & ~std::size_t{3U};
  if (!exact_size(request, 12U + padded))
    return error(context, request, x11::CoreErrorCode::BadLength);
  const auto name = std::string_view(
      reinterpret_cast<const char*>(request.bytes.data() + 12), name_length);
  if (!matches_fixed_font(name))
    return error(context, request, x11::CoreErrorCode::BadName);
  switch (state.resources().open_font(context.client_id, context.resource_base,
                                      context.resource_mask, xid)) {
    case OpenFontStatus::Success: return {};
    case OpenFontStatus::BadIdChoice:
      return error(context, request, x11::CoreErrorCode::BadIDChoice, xid);
    case OpenFontStatus::BadAlloc:
      return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return {};
}

DispatchResult close_font(ServerState& state, const DispatchContext& context,
                          const x11::FramedRequest& request) {
  if (!exact_size(request, 8)) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{}; (void)reader.read_u32(xid);
  return state.resources().close_font(xid) == CloseFontStatus::Success
      ? DispatchResult{} : error(context, request, x11::CoreErrorCode::BadFont, xid);
}

void write_char_info(x11::ByteWriter& writer) {
  writer.write_u16(0); writer.write_u16(kFixedFontAdvance);
  writer.write_u16(kFixedFontAdvance); writer.write_u16(kFixedFontAscent);
  writer.write_u16(kFixedFontDescent); writer.write_u16(0);
}

DispatchResult query_font(ServerState& state, const DispatchContext& context,
                          const x11::FramedRequest& request) {
  if (!exact_size(request, 8)) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{}; (void)reader.read_u32(xid);
  if (!valid_fontable(state.resources(), xid))
    return error(context, request, x11::CoreErrorCode::BadFont, xid);
  x11::ByteWriter reply(context.byte_order);
  reply.write_u8(1); reply.write_u8(0); reply.write_u16(x11::wire_sequence(context.sequence));
  constexpr std::uint32_t characters = kFixedFontLastCharacter - kFixedFontFirstCharacter + 1U;
  constexpr std::uint32_t extra_bytes = 60U - 32U + characters * 12U;
  reply.write_u32(extra_bytes / 4U);
  write_char_info(reply); reply.write_padding(4); write_char_info(reply); reply.write_padding(4);
  reply.write_u16(kFixedFontFirstCharacter); reply.write_u16(kFixedFontLastCharacter);
  reply.write_u16(kFixedFontDefaultCharacter); reply.write_u16(0);
  reply.write_u8(0); reply.write_u8(0); reply.write_u8(0); reply.write_u8(1);
  reply.write_u16(kFixedFontAscent); reply.write_u16(kFixedFontDescent);
  reply.write_u32(characters);
  for (std::uint32_t index = 0; index < characters; ++index) write_char_info(reply);
  return {std::move(reply).take()};
}

DispatchResult query_text_extents(ServerState& state,
                                  const DispatchContext& context,
                                  const x11::FramedRequest& request) {
  if (request.bytes.size() < 8 || (request.bytes.size() & 3U) != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{}; (void)reader.read_u32(xid);
  if (!valid_fontable(state.resources(), xid))
    return error(context, request, x11::CoreErrorCode::BadFont, xid);
  const auto bytes_after_font = request.bytes.size() - 8U;
  if ((bytes_after_font & 1U) != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  const std::size_t characters = bytes_after_font / 2U - (request.data ? 1U : 0U);
  if (request.data > 1 || characters * 2U + (request.data ? 2U : 0U) != bytes_after_font)
    return error(context, request, x11::CoreErrorCode::BadLength);
  for (std::size_t index = 0; index < characters; ++index) {
    std::uint8_t byte1{}, byte2{}; (void)reader.read_u8(byte1); (void)reader.read_u8(byte2);
    if (byte1 != 0) return error(context, request, x11::CoreErrorCode::BadImplementation);
  }
  x11::ReplyBuilder reply(context.byte_order, context.sequence, 0);
  reply.write_u16(kFixedFontAscent); reply.write_u16(kFixedFontDescent);
  reply.write_u16(kFixedFontAscent); reply.write_u16(kFixedFontDescent);
  const auto width = static_cast<std::uint32_t>(characters * kFixedFontAdvance);
  reply.write_u32(width); reply.write_u32(0); reply.write_u32(width);
  reply.write_padding(4);
  return {std::move(reply).finish()};
}

DispatchResult list_fonts(const DispatchContext& context,
                          const x11::FramedRequest& request) {
  if (request.bytes.size() < 8) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint16_t maximum{}, pattern_length{};
  (void)reader.read_u16(maximum); (void)reader.read_u16(pattern_length);
  const auto padded = (static_cast<std::size_t>(pattern_length) + 3U) & ~std::size_t{3U};
  if (!exact_size(request, 8U + padded))
    return error(context, request, x11::CoreErrorCode::BadLength);
  const auto pattern = std::string_view(
      reinterpret_cast<const char*>(request.bytes.data() + 8), pattern_length);
  const bool match = maximum != 0 && matches_fixed_font(pattern);
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u16(match ? 1 : 0); reply.write_padding(22);
  if (match) {
    constexpr std::array<std::uint8_t, 6> fixed_name{'\x05','f','i','x','e','d'};
    reply.write_payload(fixed_name);
  }
  return {std::move(reply).finish()};
}

DispatchResult image_text8(ServerState& state, const DispatchContext& context,
                           const x11::FramedRequest& request) {
  const auto padded = (static_cast<std::size_t>(request.data) + 3U) & ~std::size_t{3U};
  if (!exact_size(request, 16U + padded))
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t drawable{}, gc_id{}; std::uint16_t raw_x{}, raw_y{};
  (void)reader.read_u32(drawable); (void)reader.read_u32(gc_id);
  (void)reader.read_u16(raw_x); (void)reader.read_u16(raw_y);
  const auto* gc = state.resources().find_gc(gc_id);
  if (!gc) return error(context, request, x11::CoreErrorCode::BadGContext, gc_id);
  if (!state.resources().find_pixmap(drawable) && !supported_window_drawable(state.resources(), drawable))
    return error(context, request, known_drawable(state.resources(), drawable)
        ? x11::CoreErrorCode::BadMatch : x11::CoreErrorCode::BadDrawable, drawable);
  auto* storage = mutable_storage(state.resources(), drawable);
  if (!storage) return error(context, request, x11::CoreErrorCode::BadAlloc);
  const auto text = std::span<const std::uint8_t>(request.bytes).subspan(16, request.data);
  const auto raster = raster_text8(*storage, static_cast<std::int16_t>(raw_x),
      static_cast<std::int16_t>(raw_y), text, gc->foreground, gc->background,
      gc->plane_mask, true);
  DispatchResult result;
  if (!raster.damage.empty() && supported_window_drawable(state.resources(), drawable))
    result.drawable_damage.push_back({drawable, raster.damage});
  return result;
}

DispatchResult poly_text8(ServerState& state, const DispatchContext& context,
                          const x11::FramedRequest& request) {
  if (request.bytes.size() < 16 || (request.bytes.size() & 3U) != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t drawable{}, gc_id{}; std::uint16_t raw_x{}, raw_y{};
  (void)reader.read_u32(drawable); (void)reader.read_u32(gc_id);
  (void)reader.read_u16(raw_x); (void)reader.read_u16(raw_y);
  const auto* gc = state.resources().find_gc(gc_id);
  if (!gc) return error(context, request, x11::CoreErrorCode::BadGContext, gc_id);
  if (!state.resources().find_pixmap(drawable) && !supported_window_drawable(state.resources(), drawable))
    return error(context, request, known_drawable(state.resources(), drawable)
        ? x11::CoreErrorCode::BadMatch : x11::CoreErrorCode::BadDrawable, drawable);
  struct TextItem { std::int8_t delta{}; std::vector<std::uint8_t> text; };
  std::vector<TextItem> items;
  while (reader.remaining() != 0) {
    std::uint8_t length{}; (void)reader.read_u8(length);
    if (length == 0) continue;
    if (length == 255) {
      std::uint32_t font{}; if (!reader.read_u32(font)) return error(context, request, x11::CoreErrorCode::BadLength);
      if (!state.resources().find_font(font)) return error(context, request, x11::CoreErrorCode::BadFont, font);
      continue;
    }
    std::uint8_t delta{}; if (!reader.read_u8(delta) || reader.remaining() < length)
      return error(context, request, x11::CoreErrorCode::BadLength);
    TextItem item{static_cast<std::int8_t>(delta), {}}; item.text.reserve(length);
    for (std::uint8_t index = 0; index < length; ++index) {
      std::uint8_t value{}; (void)reader.read_u8(value); item.text.push_back(value);
    }
    items.push_back(std::move(item));
  }
  auto* storage = mutable_storage(state.resources(), drawable);
  if (!storage) return error(context, request, x11::CoreErrorCode::BadAlloc);
  std::int32_t x = static_cast<std::int16_t>(raw_x);
  const auto y = static_cast<std::int16_t>(raw_y);
  geometry::Region damage({0, 0, storage->width(), storage->height()});
  for (const auto& item : items) {
    x += item.delta;
    const auto raster = raster_text8(*storage, x, y, item.text, gc->foreground,
                                    gc->background, gc->plane_mask, false);
    if (!raster.damage.empty()) damage.add(raster.damage);
    x += static_cast<std::int32_t>(item.text.size()) * kFixedFontAdvance;
  }
  DispatchResult result;
  if (supported_window_drawable(state.resources(), drawable))
    for (const auto& rectangle : damage.rectangles())
      result.drawable_damage.push_back({drawable, rectangle});
  return result;
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
  if (!valid) return error(context, request, known_drawable(state.resources(), drawable)
      ? x11::CoreErrorCode::BadMatch : x11::CoreErrorCode::BadDrawable, drawable);
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

DispatchResult alloc_color(const ServerState& state, const DispatchContext& context,
                           const x11::FramedRequest& request) {
  if (!exact_size(request, 16)) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t colormap{}; Color color{};
  if (!reader.read_u32(colormap) || !reader.read_u16(color.red) ||
      !reader.read_u16(color.green) || !reader.read_u16(color.blue) || !reader.skip(2))
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (colormap != state.screen().default_colormap)
    return error(context, request, x11::CoreErrorCode::BadColormap, colormap);
  color = quantize_color(color);
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  reply.write_u16(color.red); reply.write_u16(color.green); reply.write_u16(color.blue);
  reply.write_padding(2); reply.write_u32(color_pixel(color));
  return {std::move(reply).finish()};
}

DispatchResult named_color(const ServerState& state, const DispatchContext& context,
                           const x11::FramedRequest& request, const bool allocate) {
  if (request.bytes.size() < 12) return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t colormap{}; std::uint16_t name_length{};
  if (!reader.read_u32(colormap) || !reader.read_u16(name_length) || !reader.skip(2) ||
      !exact_size(request, 12 + padded_size(name_length)))
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (colormap != state.screen().default_colormap)
    return error(context, request, x11::CoreErrorCode::BadColormap, colormap);
  std::span<const std::uint8_t> name;
  if (!reader.read_bytes(name_length, name)) return error(context, request, x11::CoreErrorCode::BadLength);
  const auto parsed = parse_color_name(name);
  if (!parsed) return error(context, request, x11::CoreErrorCode::BadName);
  const auto color = quantize_color(*parsed);
  x11::ReplyBuilder reply(context.byte_order, context.sequence);
  if (allocate) reply.write_u32(color_pixel(color));
  reply.write_u16(color.red); reply.write_u16(color.green); reply.write_u16(color.blue);
  reply.write_u16(color.red); reply.write_u16(color.green); reply.write_u16(color.blue);
  return {std::move(reply).finish()};
}

DispatchResult free_colors(const ServerState& state, const DispatchContext& context,
                           const x11::FramedRequest& request) {
  if (request.bytes.size() < 12 || request.bytes.size() % 4 != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order); std::uint32_t colormap{};
  if (!reader.read_u32(colormap)) return error(context, request, x11::CoreErrorCode::BadLength);
  if (colormap != state.screen().default_colormap)
    return error(context, request, x11::CoreErrorCode::BadColormap, colormap);
  return {};
}

DispatchResult query_colors(const ServerState& state, const DispatchContext& context,
                            const x11::FramedRequest& request) {
  if (request.bytes.size() < 8 || request.bytes.size() % 4 != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order); std::uint32_t colormap{};
  if (!reader.read_u32(colormap)) return error(context, request, x11::CoreErrorCode::BadLength);
  if (colormap != state.screen().default_colormap)
    return error(context, request, x11::CoreErrorCode::BadColormap, colormap);
  const auto count = (request.bytes.size() - 8) / 4;
  if (count > std::numeric_limits<std::uint16_t>::max()) return error(context, request, x11::CoreErrorCode::BadAlloc);
  x11::ReplyBuilder reply(context.byte_order, context.sequence); reply.write_u16(static_cast<std::uint16_t>(count)); reply.write_padding(22);
  for (std::size_t i = 0; i < count; ++i) { std::uint32_t pixel{}; (void)reader.read_u32(pixel); reply.write_payload_u16(static_cast<std::uint16_t>(((pixel >> 16U) & 0xffU) * 257U)); reply.write_payload_u16(static_cast<std::uint16_t>(((pixel >> 8U) & 0xffU) * 257U)); reply.write_payload_u16(static_cast<std::uint16_t>((pixel & 0xffU) * 257U)); reply.write_payload_u16(0); }
  return {std::move(reply).finish()};
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
  x11::ReplyBuilder reply(context.byte_order, context.sequence, 2); constexpr std::array<std::uint8_t,16> map{50,62, 0,0, 37,105, 64,108, 0,0, 0,0, 0,0, 0,0}; reply.write_payload(map); return {std::move(reply).finish()};
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
      case x11::CoreOpcode::QueryPointer:
        return query_pointer(state, context, request);
      case x11::CoreOpcode::TranslateCoordinates:
        return translate_coordinates(state, context, request);
      case x11::CoreOpcode::GetInputFocus:
        return get_input_focus(state, context, request);
      case x11::CoreOpcode::OpenFont:
        return open_font(state, context, request);
      case x11::CoreOpcode::CloseFont:
        return close_font(state, context, request);
      case x11::CoreOpcode::QueryFont:
        return query_font(state, context, request);
      case x11::CoreOpcode::QueryTextExtents:
        return query_text_extents(state, context, request);
      case x11::CoreOpcode::ListFonts:
        return list_fonts(context, request);
      case x11::CoreOpcode::AllocColor:
        return alloc_color(state, context, request);
      case x11::CoreOpcode::AllocNamedColor:
        return named_color(state, context, request, true);
      case x11::CoreOpcode::FreeColors:
        return free_colors(state, context, request);
      case x11::CoreOpcode::QueryColors:
        return query_colors(state, context, request);
      case x11::CoreOpcode::LookupColor:
        return named_color(state, context, request, false);
      case x11::CoreOpcode::QueryExtension:
        return query_extension(context, request);
      case x11::CoreOpcode::ListExtensions:
        return list_extensions(context, request);
      case x11::CoreOpcode::GetKeyboardMapping:
        return get_keyboard_mapping(context, request);
      case x11::CoreOpcode::GetPointerMapping:
        return get_pointer_mapping(context, request);
      case x11::CoreOpcode::GetModifierMapping:
        return get_modifier_mapping(context, request);
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
      case x11::CoreOpcode::PolyLine:
        return poly_line(state, context, request);
      case x11::CoreOpcode::PolySegment:
        return poly_segment(state, context, request);
      case x11::CoreOpcode::FillPoly:
        return fill_poly(state, context, request);
      case x11::CoreOpcode::PolyFillRectangle:
        return poly_fill_rectangle(state, context, request);
      case x11::CoreOpcode::PolyFillArc:
        return poly_fill_arc(state, context, request);
      case x11::CoreOpcode::PutImage:
        return put_image(state, context, request);
      case x11::CoreOpcode::PolyText8:
        return poly_text8(state, context, request);
      case x11::CoreOpcode::ImageText8:
        return image_text8(state, context, request);
      case x11::CoreOpcode::NoOperation:
        return {};
      default:
        break;
    }
  } catch (const std::bad_alloc&) {
    return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return error(context, request, x11::CoreErrorCode::BadRequest);
}

}  // namespace glasswyrm::server
