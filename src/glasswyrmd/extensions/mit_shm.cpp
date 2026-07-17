#include "glasswyrmd/extensions/mit_shm.hpp"

#include "core/checked_math.hpp"
#include "glasswyrmd/extension_registry.hpp"
#include "glasswyrmd/extension_wire.hpp"
#include "glasswyrmd/request_handlers/common.hpp"
#include "glasswyrmd/request_handlers/drawable_access.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/reply.hpp"

#include <unistd.h>

#include <algorithm>
#include <limits>
#include <span>
#include <vector>

namespace glasswyrm::server::extensions {
namespace x11 = gw::protocol::x11;
using namespace request_handlers;

constexpr std::uint8_t kMitShmOpcode = 129;
constexpr std::uint8_t kQueryVersion = 0;
constexpr std::uint8_t kAttach = 1;
constexpr std::uint8_t kDetach = 2;
constexpr std::uint8_t kPutImage = 3;
constexpr std::uint8_t kGetImage = 4;
constexpr std::uint8_t kZPixmap = 2;
constexpr std::size_t kMaximumShmImageBytes = 64U * 1024U * 1024U;

DispatchResult bad_segment(const DispatchContext& context,
                           const x11::FramedRequest& request,
                           const std::uint32_t segment) {
  const auto* extension = find_extension(ExtensionKind::MitShm);
  if (!extension) return error(context, request,
                               x11::CoreErrorCode::BadImplementation);
  const auto packet = encode_extension_error(
      context.byte_order, *extension, 0, context.sequence, segment,
      request.opcode, request.data);
  return packet ? DispatchResult{*packet}
                : error(context, request,
                        x11::CoreErrorCode::BadImplementation);
}

void correct_extension_error_metadata(std::vector<std::uint8_t>& packet,
                                      const x11::ByteOrder order) {
  if (packet.size() != x11::kCoreErrorSize || packet[0] != 0) return;
  if (order == x11::ByteOrder::LittleEndian) {
    packet[8] = kPutImage;
    packet[9] = 0;
  } else {
    packet[8] = 0;
    packet[9] = kPutImage;
  }
  packet[10] = kMitShmOpcode;
}

DispatchResult query_version(const DispatchContext& context,
                             const x11::FramedRequest& request) {
  if (request.core_size() != 4)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ReplyBuilder reply(context.byte_order, context.sequence, 0);
  reply.write_u16(1);
  reply.write_u16(1);
  reply.write_u16(static_cast<std::uint16_t>(::getuid()));
  reply.write_u16(static_cast<std::uint16_t>(::getgid()));
  reply.write_u8(kZPixmap);
  reply.write_padding(15);
  return {std::move(reply).finish()};
}

DispatchResult attach(ServerState& state, const DispatchContext& context,
                      const x11::FramedRequest& request) {
  if (request.core_size() != 16)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{}, shmid{};
  std::uint8_t read_only{};
  if (!reader.read_u32(xid) || !reader.read_u32(shmid) ||
      !reader.read_u8(read_only) || !reader.skip(3))
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (read_only > 1)
    return error(context, request, x11::CoreErrorCode::BadValue, read_only);
  if (!context.peer_uid)
    return error(context, request, x11::CoreErrorCode::BadAccess, shmid);
  switch (state.resources().attach_shm_segment(
      context.client_id, context.resource_base, context.resource_mask, xid,
      shmid, read_only != 0, *context.peer_uid)) {
    case AttachShmStatus::Success: return {};
    case AttachShmStatus::BadIdChoice:
      return error(context, request, x11::CoreErrorCode::BadIDChoice, xid);
    case AttachShmStatus::BadValue:
      return error(context, request, x11::CoreErrorCode::BadValue, shmid);
    case AttachShmStatus::BadAccess:
      return error(context, request, x11::CoreErrorCode::BadAccess, shmid);
    case AttachShmStatus::BadAlloc:
      return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return error(context, request, x11::CoreErrorCode::BadImplementation);
}

DispatchResult detach(ServerState& state, const DispatchContext& context,
                      const x11::FramedRequest& request) {
  if (request.core_size() != 8)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t xid{};
  (void)reader.read_u32(xid);
  return state.resources().detach_shm_segment(xid) ==
                 DetachShmStatus::Success
             ? DispatchResult{}
             : bad_segment(context, request, xid);
}

std::optional<std::vector<std::uint8_t>> crop_source(
    const ShmSegmentResource& segment, const std::uint32_t offset,
    const std::uint16_t total_width, const std::uint16_t total_height,
    const std::uint16_t source_x, const std::uint16_t source_y,
    const std::uint16_t width, const std::uint16_t height) {
  const auto stride = gw::core::checked_multiply(
      static_cast<std::size_t>(total_width), std::size_t{4});
  const auto image_size = stride ? gw::core::checked_multiply(
                                       *stride,
                                       static_cast<std::size_t>(total_height))
                                 : std::nullopt;
  const auto image_end = image_size
                             ? gw::core::checked_add(
                                   static_cast<std::size_t>(offset), *image_size)
                             : std::nullopt;
  const auto payload_size = gw::core::checked_multiply(
      static_cast<std::size_t>(width) * 4U,
      static_cast<std::size_t>(height));
  if (!stride || !image_size || !image_end || *image_end > segment.size ||
      !payload_size || *payload_size > kMaximumShmImageBytes ||
      static_cast<std::uint32_t>(source_x) + width > total_width ||
      static_cast<std::uint32_t>(source_y) + height > total_height)
    return std::nullopt;
  std::vector<std::uint8_t> payload(*payload_size);
  const auto source = segment.mapping->bytes();
  const auto row_bytes = static_cast<std::size_t>(width) * 4U;
  for (std::size_t row = 0; row < height; ++row) {
    const auto source_offset = static_cast<std::size_t>(offset) +
                               (static_cast<std::size_t>(source_y) + row) *
                                   *stride +
                               static_cast<std::size_t>(source_x) * 4U;
    std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(source_offset),
                row_bytes,
                payload.begin() + static_cast<std::ptrdiff_t>(row * row_bytes));
  }
  return payload;
}

x11::FramedRequest core_put_request(
    const DispatchContext& context, const std::uint32_t drawable,
    const std::uint32_t gc, const std::uint16_t width,
    const std::uint16_t height, const std::int16_t x, const std::int16_t y,
    const std::span<const std::uint8_t> payload) {
  x11::ByteWriter writer(context.byte_order);
  writer.write_u8(static_cast<std::uint8_t>(x11::CoreOpcode::PutImage));
  writer.write_u8(kZPixmap);
  writer.write_u16(0);
  writer.write_u32(drawable);
  writer.write_u32(gc);
  writer.write_u16(width);
  writer.write_u16(height);
  writer.write_u16(static_cast<std::uint16_t>(x));
  writer.write_u16(static_cast<std::uint16_t>(y));
  writer.write_u8(0);
  writer.write_u8(24);
  writer.write_u16(0);
  writer.write_bytes(payload);
  x11::FramedRequest result;
  result.opcode = static_cast<std::uint8_t>(x11::CoreOpcode::PutImage);
  result.data = kZPixmap;
  result.length_units = static_cast<std::uint32_t>(writer.size() / 4U);
  result.bytes = std::move(writer).take();
  return result;
}

std::vector<std::uint8_t> completion_event(
    const DispatchContext& context, const std::uint32_t drawable,
    const std::uint32_t shmseg, const std::uint32_t offset) {
  x11::ByteWriter body(context.byte_order);
  body.write_u32(drawable);
  body.write_u16(kPutImage);
  body.write_u8(kMitShmOpcode);
  body.write_u8(0);
  body.write_u32(shmseg);
  body.write_u32(offset);
  const auto* extension = find_extension(ExtensionKind::MitShm);
  if (!extension) return {};
  const auto event = encode_extension_event(
      context.byte_order, *extension, 0, context.sequence, 0,
      std::move(body).take());
  return event.value_or(std::vector<std::uint8_t>{});
}

DispatchResult put_image(ServerState& state, const DispatchContext& context,
                         const x11::FramedRequest& request) {
  if (request.core_size() != 40)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t drawable{}, gc{}, shmseg{}, offset{};
  std::uint16_t total_width{}, total_height{}, source_x{}, source_y{}, width{},
      height{}, destination_x{}, destination_y{};
  std::uint8_t depth{}, format{}, send_event{};
  if (!reader.read_u32(drawable) || !reader.read_u32(gc) ||
      !reader.read_u16(total_width) || !reader.read_u16(total_height) ||
      !reader.read_u16(source_x) || !reader.read_u16(source_y) ||
      !reader.read_u16(width) || !reader.read_u16(height) ||
      !reader.read_u16(destination_x) || !reader.read_u16(destination_y) ||
      !reader.read_u8(depth) || !reader.read_u8(format) ||
      !reader.read_u8(send_event) || !reader.skip(1) ||
      !reader.read_u32(shmseg) || !reader.read_u32(offset))
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (format != kZPixmap || send_event > 1)
    return error(context, request, x11::CoreErrorCode::BadValue,
                 format != kZPixmap ? format : send_event);
  if (depth != 24)
    return error(context, request, x11::CoreErrorCode::BadMatch, depth);
  const auto* segment = state.resources().find_shm_segment(shmseg);
  if (!segment) return bad_segment(context, request, shmseg);
  const auto payload = crop_source(*segment, offset, total_width, total_height,
                                   source_x, source_y, width, height);
  if (!payload)
    return error(context, request, x11::CoreErrorCode::BadValue, offset);
  auto result = request_handlers::put_image(
      state, context,
      core_put_request(context, drawable, gc, width, height,
                       static_cast<std::int16_t>(destination_x),
                       static_cast<std::int16_t>(destination_y), *payload));
  if (!result.output.empty()) {
    correct_extension_error_metadata(result.output, context.byte_order);
    return result;
  }
  if (send_event != 0) result.output = completion_event(
      context, drawable, shmseg, offset);
  return result;
}

DispatchResult get_image(ServerState& state, const DispatchContext& context,
                         const x11::FramedRequest& request) {
  if (request.core_size() != 32)
    return error(context, request, x11::CoreErrorCode::BadLength);
  x11::ByteReader reader(request.body(), context.byte_order);
  std::uint32_t drawable{}, plane_mask{}, shmseg{}, offset{};
  std::uint16_t raw_x{}, raw_y{}, width{}, height{};
  std::uint8_t format{};
  if (!reader.read_u32(drawable) || !reader.read_u16(raw_x) ||
      !reader.read_u16(raw_y) || !reader.read_u16(width) ||
      !reader.read_u16(height) || !reader.read_u32(plane_mask) ||
      !reader.read_u8(format) || !reader.skip(3) ||
      !reader.read_u32(shmseg) || !reader.read_u32(offset))
    return error(context, request, x11::CoreErrorCode::BadLength);
  if (format != kZPixmap)
    return error(context, request, x11::CoreErrorCode::BadValue, format);
  auto* segment = state.resources().find_shm_segment(shmseg);
  if (!segment) return bad_segment(context, request, shmseg);
  if (segment->read_only)
    return error(context, request, x11::CoreErrorCode::BadAccess, shmseg);
  auto* storage = mutable_storage(state.resources(), drawable);
  const auto* window = state.resources().find_window(drawable);
  const auto* pixmap = state.resources().find_pixmap(drawable);
  if (!storage || (!window && !pixmap))
    return error(context, request, x11::CoreErrorCode::BadDrawable, drawable);
  const auto x = static_cast<std::int16_t>(raw_x);
  const auto y = static_cast<std::int16_t>(raw_y);
  if (x < 0 || y < 0 || static_cast<std::uint32_t>(x) + width > storage->width() ||
      static_cast<std::uint32_t>(y) + height > storage->height())
    return error(context, request, x11::CoreErrorCode::BadMatch, drawable);
  const auto size = gw::core::checked_multiply(
      static_cast<std::size_t>(width) * 4U, static_cast<std::size_t>(height));
  const auto end = size ? gw::core::checked_add(
                              static_cast<std::size_t>(offset), *size)
                        : std::nullopt;
  if (!size || !end || *size > kMaximumShmImageBytes || *end > segment->size)
    return error(context, request, x11::CoreErrorCode::BadValue, offset);
  auto destination = segment->mapping->mutable_bytes().subspan(offset, *size);
  std::size_t cursor = 0;
  for (std::uint32_t row = 0; row < height; ++row) {
    for (std::uint32_t column = 0; column < width; ++column) {
      const auto pixel = storage->at(static_cast<std::uint32_t>(x) + column,
                                     static_cast<std::uint32_t>(y) + row) &
                         plane_mask & 0x00ffffffU;
      destination[cursor++] = static_cast<std::uint8_t>(pixel);
      destination[cursor++] = static_cast<std::uint8_t>(pixel >> 8U);
      destination[cursor++] = static_cast<std::uint8_t>(pixel >> 16U);
      destination[cursor++] = 0;
    }
  }
  x11::ReplyBuilder reply(context.byte_order, context.sequence, 24);
  reply.write_u32(window ? state.screen().root_visual : 0);
  reply.write_u32(static_cast<std::uint32_t>(*size));
  return {std::move(reply).finish()};
}

DispatchResult dispatch_mit_shm(ServerState& state,
                               const DispatchContext& context,
                               const x11::FramedRequest& request) {
  switch (request.data) {
    case kQueryVersion: return query_version(context, request);
    case kAttach: return attach(state, context, request);
    case kDetach: return detach(state, context, request);
    case kPutImage: return put_image(state, context, request);
    case kGetImage: return get_image(state, context, request);
    default:
      return error(context, request, x11::CoreErrorCode::BadRequest);
  }
}

}  // namespace glasswyrm::server::extensions
