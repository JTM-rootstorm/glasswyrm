#include "glasswyrmd/extensions/render_internal.hpp"

#include "glasswyrmd/picture.hpp"
#include "glasswyrmd/request_handlers/common.hpp"
#include "protocol/x11/byte_cursor.hpp"

#include <bit>
#include <cstdint>
#include <optional>
#include <vector>

namespace glasswyrm::server::extensions {
namespace x11 = gw::protocol::x11;
using request_handlers::error;

namespace {

constexpr std::uint32_t kSupportedAttributes = 0x1133U;
constexpr std::uint32_t kKnownAttributes = 0x1FFFU;

std::optional<PictureAttributeUpdate> decode_attributes(
    x11::ByteReader& reader, const std::uint32_t mask) {
  PictureAttributeUpdate update;
  update.unsupported_mask = mask & ~kSupportedAttributes;
  for (std::uint32_t bit = 0; bit < 32; ++bit) {
    if ((mask & (1U << bit)) == 0) continue;
    std::uint32_t value{};
    if (!reader.read_u32(value)) return std::nullopt;
    switch (bit) {
      case 0: update.repeat = value; break;
      case 1: update.alpha_map = value; break;
      case 4: update.clip_x_origin = std::bit_cast<std::int32_t>(value); break;
      case 5: update.clip_y_origin = std::bit_cast<std::int32_t>(value); break;
      case 8: update.subwindow_mode = value; break;
      case 12: update.component_alpha = value; break;
      default: break;
    }
  }
  return update;
}

DispatchResult picture_status(const PictureStatus status,
                              const DispatchContext& context,
                              const x11::FramedRequest& request) {
  switch (status) {
    case PictureStatus::Success: return {};
    case PictureStatus::BadAlloc:
    case PictureStatus::TooManyClipRectangles:
      return error(context, request, x11::CoreErrorCode::BadAlloc);
    case PictureStatus::UnknownFormat:
      return error(context, request, x11::CoreErrorCode::BadMatch);
    case PictureStatus::DrawableFormatMismatch:
      return error(context, request, x11::CoreErrorCode::BadMatch);
    case PictureStatus::InvalidPremultipliedColor:
    case PictureStatus::UnsupportedAttribute:
    case PictureStatus::BadAttributeValue:
    case PictureStatus::InvalidClipRectangle:
      return error(context, request, x11::CoreErrorCode::BadValue);
  }
  return error(context, request, x11::CoreErrorCode::BadImplementation);
}

std::optional<std::pair<std::uint8_t, std::uint8_t>> drawable_format(
    const ResourceTable& resources, const std::uint32_t drawable) {
  if (const auto* window = resources.find_window(drawable)) {
    if (window->window_class != WindowClass::InputOutput) return std::nullopt;
    return std::pair{window->depth, std::uint8_t{32}};
  }
  if (const auto* pixmap = resources.find_pixmap(drawable))
    return std::pair{pixmap->depth,
                     static_cast<std::uint8_t>(pixmap->depth == 1
                                                   ? 1
                                                   : pixmap->depth == 8 ? 8
                                                                        : 32)};
  return std::nullopt;
}

DispatchResult create_picture(ServerState& state,
                              const DispatchContext& context,
                              const x11::FramedRequest& request) {
  if (request.core_size() < 20 || (request.core_size() - 20) % 4 != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{}, drawable{}, raw_format{}, mask{};
  (void)reader.read_u32(xid);
  (void)reader.read_u32(drawable);
  (void)reader.read_u32(raw_format);
  (void)reader.read_u32(mask);
  if (static_cast<std::size_t>(std::popcount(mask)) !=
      (request.core_size() - 20) / 4)
    return error(context, request, x11::CoreErrorCode::BadLength);
  const auto update = decode_attributes(reader, mask);
  if (!update || reader.remaining() != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  if ((mask & ~kKnownAttributes) != 0)
    return error(context, request, x11::CoreErrorCode::BadValue, mask);
  const auto format_id = static_cast<PictureFormatId>(raw_format);
  if (!find_picture_format(format_id))
    return render_extension_error(context, request, 0, raw_format);
  const auto drawable_description = drawable_format(state.resources(), drawable);
  if (!drawable_description)
    return error(context, request, x11::CoreErrorCode::BadDrawable, drawable);
  auto picture = Picture::create_drawable(drawable, format_id,
                                          drawable_description->first,
                                          drawable_description->second);
  if (!picture)
    return error(context, request, x11::CoreErrorCode::BadMatch, raw_format);
  const auto attribute_status = picture->apply_attributes(*update);
  if (attribute_status != PictureStatus::Success)
    return picture_status(attribute_status, context, request);
  switch (state.resources().create_picture(
      context.client_id, context.resource_base, context.resource_mask, xid,
      std::move(*picture))) {
    case PictureResourceStatus::Success: return {};
    case PictureResourceStatus::BadIdChoice:
      return error(context, request, x11::CoreErrorCode::BadIDChoice, xid);
    case PictureResourceStatus::BadAlloc:
      return error(context, request, x11::CoreErrorCode::BadAlloc);
    default:
      return error(context, request, x11::CoreErrorCode::BadImplementation);
  }
}

DispatchResult change_picture(ServerState& state,
                              const DispatchContext& context,
                              const x11::FramedRequest& request) {
  if (request.core_size() < 12 || (request.core_size() - 12) % 4 != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{}, mask{};
  (void)reader.read_u32(xid);
  (void)reader.read_u32(mask);
  if (static_cast<std::size_t>(std::popcount(mask)) !=
      (request.core_size() - 12) / 4)
    return error(context, request, x11::CoreErrorCode::BadLength);
  const auto update = decode_attributes(reader, mask);
  if (!update || reader.remaining() != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  auto* picture = state.resources().find_picture(xid);
  if (!picture) return render_extension_error(context, request, 1, xid);
  if ((mask & ~kKnownAttributes) != 0)
    return error(context, request, x11::CoreErrorCode::BadValue, mask);
  return picture_status(picture->apply_attributes(*update), context, request);
}

DispatchResult set_clip(ServerState& state, const DispatchContext& context,
                        const x11::FramedRequest& request) {
  if (request.core_size() < 12 || (request.core_size() - 12) % 8 != 0)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{};
  std::uint16_t raw_x{}, raw_y{};
  (void)reader.read_u32(xid);
  (void)reader.read_u16(raw_x);
  (void)reader.read_u16(raw_y);
  std::vector<geometry::Rectangle> rectangles;
  try {
    rectangles.reserve((request.core_size() - 12) / 8);
    while (reader.remaining() != 0) {
      std::uint16_t raw_rectangle_x{}, raw_rectangle_y{}, width{}, height{};
      (void)reader.read_u16(raw_rectangle_x);
      (void)reader.read_u16(raw_rectangle_y);
      (void)reader.read_u16(width);
      (void)reader.read_u16(height);
      rectangles.push_back({std::bit_cast<std::int16_t>(raw_rectangle_x),
                            std::bit_cast<std::int16_t>(raw_rectangle_y), width,
                            height});
    }
  } catch (const std::bad_alloc&) {
    return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  auto* picture = state.resources().find_picture(xid);
  if (!picture) return render_extension_error(context, request, 1, xid);
  return picture_status(
      picture->set_clip_rectangles(std::bit_cast<std::int16_t>(raw_x),
                                   std::bit_cast<std::int16_t>(raw_y),
                                   rectangles),
      context, request);
}

DispatchResult free_picture(ServerState& state,
                            const DispatchContext& context,
                            const x11::FramedRequest& request) {
  if (request.core_size() != 8)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{};
  (void)reader.read_u32(xid);
  if (state.resources().free_picture(xid) != PictureResourceStatus::Success)
    return render_extension_error(context, request, 1, xid);
  return {};
}

DispatchResult create_solid(ServerState& state,
                            const DispatchContext& context,
                            const x11::FramedRequest& request) {
  if (request.core_size() != 16)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{};
  std::uint16_t red{}, green{}, blue{}, alpha{};
  (void)reader.read_u32(xid);
  (void)reader.read_u16(red);
  (void)reader.read_u16(green);
  (void)reader.read_u16(blue);
  (void)reader.read_u16(alpha);
  const auto color = render_color_from_u16(red, green, blue, alpha);
  if (!color) return error(context, request, x11::CoreErrorCode::BadValue);
  auto picture = Picture::create_solid(*color);
  if (!picture) return error(context, request, x11::CoreErrorCode::BadValue);
  const auto status = state.resources().create_picture(
      context.client_id, context.resource_base, context.resource_mask, xid,
      std::move(*picture));
  if (status == PictureResourceStatus::BadIdChoice)
    return error(context, request, x11::CoreErrorCode::BadIDChoice, xid);
  if (status == PictureResourceStatus::BadAlloc)
    return error(context, request, x11::CoreErrorCode::BadAlloc);
  return status == PictureResourceStatus::Success
             ? DispatchResult{}
             : error(context, request, x11::CoreErrorCode::BadImplementation);
}

}  // namespace

DispatchResult render_picture_request(ServerState& state,
                                      const DispatchContext& context,
                                      const x11::FramedRequest& request) {
  switch (request.data) {
    case 4: return create_picture(state, context, request);
    case 5: return change_picture(state, context, request);
    case 6: return set_clip(state, context, request);
    case 7: return free_picture(state, context, request);
    case 33: return create_solid(state, context, request);
    default: return error(context, request, x11::CoreErrorCode::BadRequest);
  }
}

}  // namespace glasswyrm::server::extensions
