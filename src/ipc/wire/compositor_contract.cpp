#include "ipc/wire/compositor_contract.hpp"

#include "ipc/wire/byte_reader.hpp"
#include "ipc/wire/byte_writer.hpp"

#include <limits>
#include <utility>

namespace gw::ipc::wire {
namespace {

template <typename Enum>
[[nodiscard]] bool in_range(const Enum value, const Enum first,
                            const Enum last) noexcept {
  return value >= first && value <= last;
}

void write_color(ByteWriter &writer, const SdrColorMetadata &color) {
  writer.u16(static_cast<std::uint16_t>(color.color_space));
  writer.u16(static_cast<std::uint16_t>(color.transfer_function));
  writer.u16(static_cast<std::uint16_t>(color.primaries));
  writer.u8(color.luminance_available ? 1U : 0U);
  writer.u8(0);
  writer.u32(color.minimum_luminance_millinit);
  writer.u32(color.maximum_luminance_millinit);
  writer.u32(color.max_frame_average_luminance_millinit);
}

[[nodiscard]] CodecStatus read_color(ByteReader &reader,
                                     SdrColorMetadata &color) {
  std::uint16_t color_space = 0;
  std::uint16_t transfer = 0;
  std::uint16_t primaries = 0;
  std::uint8_t available = 0;
  std::uint8_t reserved = 0;
  if (!reader.u16(color_space) || !reader.u16(transfer) ||
      !reader.u16(primaries) || !reader.u8(available) || !reader.u8(reserved) ||
      !reader.u32(color.minimum_luminance_millinit) ||
      !reader.u32(color.maximum_luminance_millinit) ||
      !reader.u32(color.max_frame_average_luminance_millinit)) {
    return CodecStatus::Truncated;
  }
  color.color_space = static_cast<SdrColorSpace>(color_space);
  color.transfer_function = static_cast<TransferFunction>(transfer);
  color.primaries = static_cast<ColorPrimaries>(primaries);
  color.luminance_available = available == 1;
  const bool valid =
      reserved == 0 && available <= 1 &&
      in_range(color.color_space, SdrColorSpace::Srgb,
               SdrColorSpace::DisplayP3) &&
      in_range(color.transfer_function, TransferFunction::Srgb,
               TransferFunction::Linear) &&
      in_range(color.primaries, ColorPrimaries::Srgb,
               ColorPrimaries::DisplayP3) &&
      (color.luminance_available
           ? (color.maximum_luminance_millinit != 0 &&
              color.minimum_luminance_millinit <=
                  color.maximum_luminance_millinit &&
              color.max_frame_average_luminance_millinit <=
                  color.maximum_luminance_millinit)
           : (color.minimum_luminance_millinit == 0 &&
              color.maximum_luminance_millinit == 0 &&
              color.max_frame_average_luminance_millinit == 0));
  return valid ? CodecStatus::Ok : CodecStatus::InvalidValue;
}

[[nodiscard]] CodecStatus final_status(const ByteReader &reader,
                                       const bool valid) noexcept {
  if (!reader.done()) {
    return CodecStatus::TrailingData;
  }
  return valid ? CodecStatus::Ok : CodecStatus::InvalidValue;
}

void write_id(ByteWriter &writer, const std::uint64_t id) { writer.u64(id); }

template <typename Value>
[[nodiscard]] CodecStatus decode_id(const std::span<const std::uint8_t> bytes,
                                    Value &value, std::uint64_t Value::*member) {
  ByteReader reader(bytes);
  Value decoded;
  if (!reader.u64(decoded.*member)) {
    return CodecStatus::Truncated;
  }
  const auto status = final_status(reader, decoded.*member != 0);
  if (status == CodecStatus::Ok) {
    value = decoded;
  }
  return status;
}

[[nodiscard]] bool valid_rectangle(const DamageRectangle &rectangle) noexcept {
  if (rectangle.width == 0 || rectangle.height == 0) {
    return false;
  }
  const auto max = std::numeric_limits<std::int32_t>::max();
  return rectangle.width - 1U <=
             static_cast<std::uint64_t>(max) -
                 static_cast<std::int64_t>(rectangle.x) &&
         rectangle.height - 1U <=
             static_cast<std::uint64_t>(max) -
                 static_cast<std::int64_t>(rectangle.y);
}

} // namespace

std::vector<std::uint8_t> encode(const OutputUpsert &value) {
  ByteWriter writer;
  writer.u64(value.output_id);
  writer.u8(value.enabled ? 1U : 0U);
  writer.u8(0);
  writer.u16(static_cast<std::uint16_t>(value.transform));
  writer.i32(value.logical_x);
  writer.i32(value.logical_y);
  writer.u32(value.logical_width);
  writer.u32(value.logical_height);
  writer.u32(value.physical_pixel_width);
  writer.u32(value.physical_pixel_height);
  writer.u32(value.refresh_millihertz);
  writer.u32(value.scale_numerator);
  writer.u32(value.scale_denominator);
  write_color(writer, value.color);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   OutputUpsert &value) {
  ByteReader reader(bytes);
  OutputUpsert decoded;
  std::uint8_t enabled = 0;
  std::uint8_t reserved = 0;
  std::uint16_t transform = 0;
  if (!reader.u64(decoded.output_id) || !reader.u8(enabled) ||
      !reader.u8(reserved) || !reader.u16(transform) ||
      !reader.i32(decoded.logical_x) || !reader.i32(decoded.logical_y) ||
      !reader.u32(decoded.logical_width) ||
      !reader.u32(decoded.logical_height) ||
      !reader.u32(decoded.physical_pixel_width) ||
      !reader.u32(decoded.physical_pixel_height) ||
      !reader.u32(decoded.refresh_millihertz) ||
      !reader.u32(decoded.scale_numerator) ||
      !reader.u32(decoded.scale_denominator)) {
    return CodecStatus::Truncated;
  }
  const auto color_status = read_color(reader, decoded.color);
  if (color_status != CodecStatus::Ok) {
    return color_status;
  }
  decoded.enabled = enabled == 1;
  decoded.transform = static_cast<Transform>(transform);
  const bool valid = decoded.output_id != 0 && enabled <= 1 && reserved == 0 &&
                     decoded.scale_numerator != 0 &&
                     decoded.scale_denominator != 0 &&
                     in_range(decoded.transform, Transform::Normal,
                              Transform::Flipped270) &&
                     (!decoded.enabled ||
                      (decoded.logical_width != 0 && decoded.logical_height != 0 &&
                       decoded.physical_pixel_width != 0 &&
                       decoded.physical_pixel_height != 0 &&
                       decoded.refresh_millihertz != 0));
  const auto status = final_status(reader, valid);
  if (status == CodecStatus::Ok) {
    value = decoded;
  }
  return status;
}

std::vector<std::uint8_t> encode(const OutputRemove &value) {
  ByteWriter writer;
  write_id(writer, value.output_id);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   OutputRemove &value) {
  return decode_id(bytes, value, &OutputRemove::output_id);
}

std::vector<std::uint8_t> encode(const SurfaceUpsert &value) {
  ByteWriter writer;
  writer.u64(value.surface_id);
  writer.u32(value.x11_window_id);
  writer.u32(0);
  writer.u64(value.parent_surface_id);
  writer.u64(value.output_id);
  writer.i32(value.logical_x);
  writer.i32(value.logical_y);
  writer.u32(value.logical_width);
  writer.u32(value.logical_height);
  writer.i32(value.stacking);
  writer.u8(value.visible ? 1U : 0U);
  writer.u8(value.clipping ? 1U : 0U);
  writer.u16(static_cast<std::uint16_t>(value.transform));
  writer.i32(value.clip_x);
  writer.i32(value.clip_y);
  writer.u32(value.clip_width);
  writer.u32(value.clip_height);
  writer.u32(value.opacity);
  writer.u32(value.scale_numerator);
  writer.u32(value.scale_denominator);
  write_color(writer, value.color);
  writer.u32(value.presentation_flags);
  writer.u8(static_cast<std::uint8_t>(value.fullscreen_eligible));
  writer.u8(static_cast<std::uint8_t>(value.direct_scanout_eligible));
  writer.u16(0);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   SurfaceUpsert &value) {
  ByteReader reader(bytes);
  SurfaceUpsert decoded;
  std::uint32_t reserved1 = 0;
  std::uint8_t visible = 0;
  std::uint8_t clipping = 0;
  std::uint16_t transform = 0;
  std::uint8_t fullscreen = 0;
  std::uint8_t scanout = 0;
  std::uint16_t reserved2 = 0;
  if (!reader.u64(decoded.surface_id) || !reader.u32(decoded.x11_window_id) ||
      !reader.u32(reserved1) || !reader.u64(decoded.parent_surface_id) ||
      !reader.u64(decoded.output_id) || !reader.i32(decoded.logical_x) ||
      !reader.i32(decoded.logical_y) || !reader.u32(decoded.logical_width) ||
      !reader.u32(decoded.logical_height) || !reader.i32(decoded.stacking) ||
      !reader.u8(visible) || !reader.u8(clipping) || !reader.u16(transform) ||
      !reader.i32(decoded.clip_x) || !reader.i32(decoded.clip_y) ||
      !reader.u32(decoded.clip_width) || !reader.u32(decoded.clip_height) ||
      !reader.u32(decoded.opacity) || !reader.u32(decoded.scale_numerator) ||
      !reader.u32(decoded.scale_denominator)) {
    return CodecStatus::Truncated;
  }
  const auto color_status = read_color(reader, decoded.color);
  if (color_status != CodecStatus::Ok) {
    return color_status;
  }
  if (!reader.u32(decoded.presentation_flags) || !reader.u8(fullscreen) ||
      !reader.u8(scanout) || !reader.u16(reserved2)) {
    return CodecStatus::Truncated;
  }
  decoded.visible = visible == 1;
  decoded.clipping = clipping == 1;
  decoded.transform = static_cast<Transform>(transform);
  decoded.fullscreen_eligible = static_cast<TriState>(fullscreen);
  decoded.direct_scanout_eligible = static_cast<TriState>(scanout);
  const bool valid =
      decoded.surface_id != 0 && decoded.logical_width != 0 &&
      decoded.logical_height != 0 && visible <= 1 && clipping <= 1 &&
      reserved1 == 0 && reserved2 == 0 && decoded.opacity <= kOpacityOne &&
      decoded.scale_numerator != 0 && decoded.scale_denominator != 0 &&
      in_range(decoded.transform, Transform::Normal, Transform::Flipped270) &&
      in_range(decoded.fullscreen_eligible, TriState::Unknown, TriState::True) &&
      in_range(decoded.direct_scanout_eligible, TriState::Unknown,
               TriState::True) &&
      (decoded.presentation_flags & ~UINT32_C(1)) == 0 &&
      (!decoded.clipping ||
       valid_rectangle({decoded.clip_x, decoded.clip_y, decoded.clip_width,
                        decoded.clip_height}));
  const auto status = final_status(reader, valid);
  if (status == CodecStatus::Ok) {
    value = decoded;
  }
  return status;
}

std::vector<std::uint8_t> encode(const SurfaceRemove &value) {
  ByteWriter writer;
  write_id(writer, value.surface_id);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   SurfaceRemove &value) {
  return decode_id(bytes, value, &SurfaceRemove::surface_id);
}

std::vector<std::uint8_t> encode(const BufferAttach &value) {
  ByteWriter writer;
  writer.u64(value.buffer_id);
  writer.u64(value.surface_id);
  writer.u32(value.width);
  writer.u32(value.height);
  writer.u32(value.stride);
  writer.u32(0);
  writer.u64(value.byte_offset);
  writer.u64(value.storage_size);
  writer.u16(static_cast<std::uint16_t>(value.pixel_format));
  writer.u16(static_cast<std::uint16_t>(value.alpha_semantics));
  writer.u64(value.modifier);
  write_color(writer, value.color);
  writer.u16(static_cast<std::uint16_t>(value.synchronization));
  writer.u16(0);
  writer.u32(value.flags);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   BufferAttach &value) {
  ByteReader reader(bytes);
  BufferAttach decoded;
  std::uint32_t reserved1 = 0;
  std::uint16_t format = 0;
  std::uint16_t alpha = 0;
  std::uint16_t synchronization = 0;
  std::uint16_t reserved2 = 0;
  if (!reader.u64(decoded.buffer_id) || !reader.u64(decoded.surface_id) ||
      !reader.u32(decoded.width) || !reader.u32(decoded.height) ||
      !reader.u32(decoded.stride) || !reader.u32(reserved1) ||
      !reader.u64(decoded.byte_offset) || !reader.u64(decoded.storage_size) ||
      !reader.u16(format) || !reader.u16(alpha) ||
      !reader.u64(decoded.modifier)) {
    return CodecStatus::Truncated;
  }
  const auto color_status = read_color(reader, decoded.color);
  if (color_status != CodecStatus::Ok) {
    return color_status;
  }
  if (!reader.u16(synchronization) || !reader.u16(reserved2) ||
      !reader.u32(decoded.flags)) {
    return CodecStatus::Truncated;
  }
  decoded.pixel_format = static_cast<PixelFormat>(format);
  decoded.alpha_semantics = static_cast<AlphaSemantics>(alpha);
  decoded.synchronization = static_cast<SynchronizationMode>(synchronization);
  bool geometry_valid = decoded.width != 0 && decoded.height != 0 &&
                        decoded.width <= std::numeric_limits<std::uint32_t>::max() / 4U;
  std::uint64_t required = 0;
  if (geometry_valid) {
    const auto row_bytes = static_cast<std::uint64_t>(decoded.width) * 4U;
    geometry_valid = decoded.stride >= row_bytes;
    if (geometry_valid) {
      const auto rows = static_cast<std::uint64_t>(decoded.height - 1U);
      geometry_valid = rows == 0 ||
                       decoded.stride <=
                           (std::numeric_limits<std::uint64_t>::max() - row_bytes) /
                               rows;
      if (geometry_valid) {
        required = rows * decoded.stride + row_bytes;
        geometry_valid = decoded.byte_offset <= decoded.storage_size &&
                         required <= decoded.storage_size - decoded.byte_offset;
      }
    }
  }
  const bool format_valid =
      (decoded.pixel_format == PixelFormat::Xrgb8888 &&
       decoded.alpha_semantics == AlphaSemantics::Opaque) ||
      (decoded.pixel_format == PixelFormat::Argb8888 &&
       decoded.alpha_semantics == AlphaSemantics::Premultiplied);
  const bool valid = decoded.buffer_id != 0 && decoded.surface_id != 0 &&
                     reserved1 == 0 && reserved2 == 0 && geometry_valid &&
                     format_valid && decoded.modifier == 0 &&
                     decoded.synchronization == SynchronizationMode::None &&
                     decoded.flags == 0;
  const auto status = final_status(reader, valid);
  if (status == CodecStatus::Ok) {
    value = decoded;
  }
  return status;
}

std::vector<std::uint8_t> encode(const BufferDetach &value) {
  ByteWriter writer;
  writer.u64(value.surface_id);
  writer.u64(value.buffer_id);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   BufferDetach &value) {
  ByteReader reader(bytes);
  BufferDetach decoded;
  if (!reader.u64(decoded.surface_id) || !reader.u64(decoded.buffer_id)) {
    return CodecStatus::Truncated;
  }
  const auto status = final_status(
      reader, decoded.surface_id != 0 && decoded.buffer_id != 0);
  if (status == CodecStatus::Ok) {
    value = decoded;
  }
  return status;
}

std::vector<std::uint8_t> encode(const BufferRelease &value) {
  ByteWriter writer;
  writer.u64(value.buffer_id);
  writer.u16(static_cast<std::uint16_t>(value.reason));
  writer.u16(0);
  writer.u32(0);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   BufferRelease &value) {
  ByteReader reader(bytes);
  BufferRelease decoded;
  std::uint16_t reason = 0;
  std::uint16_t reserved1 = 0;
  std::uint32_t reserved2 = 0;
  if (!reader.u64(decoded.buffer_id) || !reader.u16(reason) ||
      !reader.u16(reserved1) || !reader.u32(reserved2)) {
    return CodecStatus::Truncated;
  }
  decoded.reason = static_cast<BufferReleaseReason>(reason);
  const auto status = final_status(
      reader, decoded.buffer_id != 0 && reserved1 == 0 && reserved2 == 0 &&
                  in_range(decoded.reason, BufferReleaseReason::Replaced,
                           BufferReleaseReason::Invalid));
  if (status == CodecStatus::Ok) {
    value = decoded;
  }
  return status;
}

std::vector<std::uint8_t> encode(const SurfaceDamage &value) {
  if (value.rectangles.size() > kMaximumDamageRectangles) {
    return {};
  }
  ByteWriter writer;
  writer.u64(value.surface_id);
  writer.u32(static_cast<std::uint32_t>(value.rectangles.size()));
  writer.u32(0);
  for (const auto &rectangle : value.rectangles) {
    writer.i32(rectangle.x);
    writer.i32(rectangle.y);
    writer.u32(rectangle.width);
    writer.u32(rectangle.height);
  }
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   SurfaceDamage &value) {
  ByteReader reader(bytes);
  SurfaceDamage decoded;
  std::uint32_t count = 0;
  std::uint32_t reserved = 0;
  if (!reader.u64(decoded.surface_id) || !reader.u32(count) ||
      !reader.u32(reserved)) {
    return CodecStatus::Truncated;
  }
  if (count > kMaximumDamageRectangles) {
    return CodecStatus::LimitExceeded;
  }
  constexpr std::size_t rectangle_size = 16;
  if (reader.remaining() != static_cast<std::size_t>(count) * rectangle_size) {
    return reader.remaining() < static_cast<std::size_t>(count) * rectangle_size
               ? CodecStatus::Truncated
               : CodecStatus::TrailingData;
  }
  decoded.rectangles.reserve(count);
  bool valid = decoded.surface_id != 0 && reserved == 0;
  for (std::uint32_t index = 0; index < count; ++index) {
    DamageRectangle rectangle;
    if (!reader.i32(rectangle.x) || !reader.i32(rectangle.y) ||
        !reader.u32(rectangle.width) || !reader.u32(rectangle.height)) {
      return CodecStatus::Truncated;
    }
    valid = valid && valid_rectangle(rectangle);
    decoded.rectangles.push_back(rectangle);
  }
  const auto status = final_status(reader, valid);
  if (status == CodecStatus::Ok) {
    value = std::move(decoded);
  }
  return status;
}

std::vector<std::uint8_t> encode(const FrameCommit &value) {
  ByteWriter writer;
  writer.u64(value.commit_id);
  writer.u64(value.output_id);
  writer.u64(value.producer_generation);
  writer.u32(value.flags);
  writer.u32(0);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   FrameCommit &value) {
  ByteReader reader(bytes);
  FrameCommit decoded;
  std::uint32_t reserved = 0;
  if (!reader.u64(decoded.commit_id) || !reader.u64(decoded.output_id) ||
      !reader.u64(decoded.producer_generation) || !reader.u32(decoded.flags) ||
      !reader.u32(reserved)) {
    return CodecStatus::Truncated;
  }
  const auto status = final_status(
      reader, decoded.commit_id != 0 && decoded.flags == 0 && reserved == 0);
  if (status == CodecStatus::Ok) {
    value = decoded;
  }
  return status;
}

std::vector<std::uint8_t> encode(const FrameAcknowledged &value) {
  ByteWriter writer;
  writer.u64(value.commit_id);
  writer.u64(value.output_id);
  writer.u64(value.presented_generation);
  writer.u16(static_cast<std::uint16_t>(value.result));
  writer.u16(0);
  writer.u32(0);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   FrameAcknowledged &value) {
  ByteReader reader(bytes);
  FrameAcknowledged decoded;
  std::uint16_t result = 0;
  std::uint16_t reserved1 = 0;
  std::uint32_t reserved2 = 0;
  if (!reader.u64(decoded.commit_id) || !reader.u64(decoded.output_id) ||
      !reader.u64(decoded.presented_generation) || !reader.u16(result) ||
      !reader.u16(reserved1) || !reader.u32(reserved2)) {
    return CodecStatus::Truncated;
  }
  decoded.result = static_cast<FrameResult>(result);
  const auto status = final_status(
      reader, decoded.commit_id != 0 && reserved1 == 0 && reserved2 == 0 &&
                  in_range(decoded.result, FrameResult::Accepted,
                           FrameResult::Dropped));
  if (status == CodecStatus::Ok) {
    value = decoded;
  }
  return status;
}

} // namespace gw::ipc::wire
