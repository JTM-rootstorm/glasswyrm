#include "ipc/wire/output_contract.hpp"

#include "ipc/wire/byte_reader.hpp"
#include "ipc/wire/byte_writer.hpp"
#include "ipc/wire/control.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <utility>

namespace gw::ipc::wire {
namespace {

constexpr std::uint32_t kMaximumPhysicalExtent = 4096;
constexpr std::uint64_t kMaximumPhysicalPixels = UINT64_C(16'777'216);
constexpr std::uint32_t kMaximumScaleDenominator = 120;
constexpr std::uint32_t kKnownTransformMask = UINT32_C(0xff);

bool reduced_scale(const std::uint32_t numerator,
                   const std::uint32_t denominator) noexcept {
  return numerator != 0 && denominator != 0 &&
         std::gcd(numerator, denominator) == 1;
}

int compare_scale(const std::uint32_t left_numerator,
                  const std::uint32_t left_denominator,
                  const std::uint32_t right_numerator,
                  const std::uint32_t right_denominator) noexcept {
  const auto left = static_cast<std::uint64_t>(left_numerator) *
                    right_denominator;
  const auto right = static_cast<std::uint64_t>(right_numerator) *
                     left_denominator;
  return left < right ? -1 : left > right ? 1 : 0;
}

bool valid_scale(const std::uint32_t numerator,
                 const std::uint32_t denominator,
                 const std::uint32_t denominator_limit =
                     kMaximumScaleDenominator) noexcept {
  return denominator <= denominator_limit && reduced_scale(numerator, denominator) &&
         compare_scale(numerator, denominator, 1, 1) >= 0 &&
         compare_scale(numerator, denominator, 4, 1) <= 0;
}

CodecStatus finish(const ByteReader& reader, const bool valid) noexcept {
  if (!reader.done()) return CodecStatus::TrailingData;
  return valid ? CodecStatus::Ok : CodecStatus::InvalidValue;
}

bool valid_extent(const std::int32_t origin, const std::uint32_t extent) noexcept {
  return extent != 0 && origin >= 0 &&
         static_cast<std::uint64_t>(origin) + extent <=
             static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) +
                 1U;
}

bool valid_output_descriptor(const OutputDescriptorUpsert& value) noexcept {
  const auto dimensions_known =
      (value.capability_flags & kOutputPhysicalDimensionsKnown) != 0;
  const auto conflicting_modes =
      (value.capability_flags & kOutputArbitraryHeadlessMode) != 0 &&
      (value.capability_flags & kOutputModeFixed) != 0;
  const auto arbitrary_drm = value.kind == OutputKind::Drm &&
                             (value.capability_flags &
                              kOutputArbitraryHeadlessMode) != 0;
  return value.output_id != 0 &&
         value.kind >= OutputKind::Headless && value.kind <= OutputKind::Drm &&
         !value.name.empty() && value.name.size() <= kMaximumOutputNameBytes &&
         valid_utf8(value.name) &&
         (value.capability_flags & ~kKnownOutputCapabilityFlags) == 0 &&
         !conflicting_modes && !arbitrary_drm &&
         (dimensions_known
              ? value.physical_width_millimeters != 0 &&
                    value.physical_height_millimeters != 0
              : value.physical_width_millimeters == 0 &&
                    value.physical_height_millimeters == 0) &&
         value.supported_transform_mask != 0 &&
         (value.supported_transform_mask & ~kKnownTransformMask) == 0 &&
         value.maximum_scale_denominator_value != 0 &&
         value.maximum_scale_denominator_value <= kMaximumScaleDenominator &&
         valid_scale(value.minimum_scale_numerator,
                     value.minimum_scale_denominator,
                     value.maximum_scale_denominator_value) &&
         valid_scale(value.maximum_scale_numerator,
                     value.maximum_scale_denominator,
                     value.maximum_scale_denominator_value) &&
         compare_scale(value.minimum_scale_numerator,
                       value.minimum_scale_denominator,
                       value.maximum_scale_numerator,
                       value.maximum_scale_denominator) <= 0 &&
         value.maximum_physical_width != 0 &&
         value.maximum_physical_width <= kMaximumPhysicalExtent &&
         value.maximum_physical_height != 0 &&
         value.maximum_physical_height <= kMaximumPhysicalExtent;
}

bool valid_mode(const OutputModeUpsert& value) noexcept {
  const auto pixels = static_cast<std::uint64_t>(value.physical_width) *
                      value.physical_height;
  return value.output_id != 0 && value.mode_id != 0 &&
         value.physical_width != 0 &&
         value.physical_width <= kMaximumPhysicalExtent &&
         value.physical_height != 0 &&
         value.physical_height <= kMaximumPhysicalExtent &&
         pixels <= kMaximumPhysicalPixels && value.refresh_millihertz != 0 &&
         value.flags == 0;
}

bool unique_ids(const std::span<const std::uint64_t> ids) noexcept {
  for (std::size_t index = 0; index < ids.size(); ++index) {
    if (ids[index] == 0) return false;
    for (std::size_t prior = 0; prior < index; ++prior)
      if (ids[prior] == ids[index]) return false;
  }
  return true;
}

bool valid_surface_state(const SurfaceOutputState& value) noexcept {
  const auto primary = std::ranges::find(value.output_ids,
                                         value.primary_output_id);
  return value.surface_id != 0 && value.primary_output_id != 0 &&
         value.output_ids.size() <= kMaximumManagedOutputs &&
         unique_ids(value.output_ids) &&
         (value.output_ids.empty() || primary != value.output_ids.end()) &&
         valid_scale(value.preferred_scale_numerator,
                     value.preferred_scale_denominator) &&
         value.client_buffer_scale >= 1 && value.client_buffer_scale <= 4 &&
         value.scale_mode >= SurfaceScaleMode::Legacy &&
         value.scale_mode <= SurfaceScaleMode::ScaledPixmap &&
         (value.scale_mode != SurfaceScaleMode::Legacy ||
          value.client_buffer_scale == 1) &&
         value.layout_generation != 0 && value.flags == 0;
}

bool valid_policy_output(const PolicyOutputUpsert& value) noexcept {
  const auto transform = static_cast<std::uint16_t>(value.transform);
  if (value.output_id == 0 || transform > 7 || value.flags != 0 ||
      !valid_scale(value.scale_numerator, value.scale_denominator) ||
      (value.primary && !value.enabled))
    return false;
  if (!value.enabled)
    return !value.primary && value.logical_x == 0 && value.logical_y == 0 &&
           value.logical_width == 0 && value.logical_height == 0 &&
           value.work_x == 0 && value.work_y == 0 && value.work_width == 0 &&
           value.work_height == 0;
  if (!valid_extent(value.logical_x, value.logical_width) ||
      !valid_extent(value.logical_y, value.logical_height) ||
      !valid_extent(value.work_x, value.work_width) ||
      !valid_extent(value.work_y, value.work_height))
    return false;
  const auto logical_right =
      static_cast<std::uint64_t>(value.logical_x) + value.logical_width;
  const auto logical_bottom =
      static_cast<std::uint64_t>(value.logical_y) + value.logical_height;
  return value.work_x >= value.logical_x && value.work_y >= value.logical_y &&
         static_cast<std::uint64_t>(value.work_x) + value.work_width <=
             logical_right &&
         static_cast<std::uint64_t>(value.work_y) + value.work_height <=
             logical_bottom;
}

}  // namespace

std::vector<std::uint8_t> encode(const OutputDescriptorUpsert& value) {
  if (value.name.size() > kMaximumOutputNameBytes) return {};
  ByteWriter writer;
  writer.u64(value.output_id);
  writer.u16(static_cast<std::uint16_t>(value.kind));
  writer.u16(static_cast<std::uint16_t>(value.name.size()));
  writer.u32(value.capability_flags);
  writer.u32(value.physical_width_millimeters);
  writer.u32(value.physical_height_millimeters);
  writer.u32(value.supported_transform_mask);
  writer.u32(value.minimum_scale_numerator);
  writer.u32(value.minimum_scale_denominator);
  writer.u32(value.maximum_scale_numerator);
  writer.u32(value.maximum_scale_denominator);
  writer.u32(value.maximum_scale_denominator_value);
  writer.u32(value.maximum_physical_width);
  writer.u32(value.maximum_physical_height);
  writer.string(value.name);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   OutputDescriptorUpsert& value) {
  ByteReader reader(bytes);
  OutputDescriptorUpsert decoded;
  std::uint16_t kind{}, name_size{};
  if (!reader.u64(decoded.output_id) || !reader.u16(kind) ||
      !reader.u16(name_size) || !reader.u32(decoded.capability_flags) ||
      !reader.u32(decoded.physical_width_millimeters) ||
      !reader.u32(decoded.physical_height_millimeters) ||
      !reader.u32(decoded.supported_transform_mask) ||
      !reader.u32(decoded.minimum_scale_numerator) ||
      !reader.u32(decoded.minimum_scale_denominator) ||
      !reader.u32(decoded.maximum_scale_numerator) ||
      !reader.u32(decoded.maximum_scale_denominator) ||
      !reader.u32(decoded.maximum_scale_denominator_value) ||
      !reader.u32(decoded.maximum_physical_width) ||
      !reader.u32(decoded.maximum_physical_height))
    return CodecStatus::Truncated;
  if (name_size > kMaximumOutputNameBytes) return CodecStatus::LimitExceeded;
  if (!reader.string(name_size, decoded.name)) return CodecStatus::Truncated;
  decoded.kind = static_cast<OutputKind>(kind);
  const auto status = finish(reader, valid_output_descriptor(decoded));
  if (status == CodecStatus::Ok) value = std::move(decoded);
  return status;
}

std::vector<std::uint8_t> encode(const OutputModeUpsert& value) {
  ByteWriter writer;
  writer.u64(value.output_id);
  writer.u64(value.mode_id);
  writer.u32(value.physical_width);
  writer.u32(value.physical_height);
  writer.u32(value.refresh_millihertz);
  writer.u8(value.preferred ? 1U : 0U);
  writer.u8(value.current ? 1U : 0U);
  writer.u16(0);
  writer.u32(value.flags);
  writer.u32(0);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   OutputModeUpsert& value) {
  ByteReader reader(bytes);
  OutputModeUpsert decoded;
  std::uint8_t preferred{}, current{};
  std::uint16_t reserved{};
  std::uint32_t reserved32{};
  if (!reader.u64(decoded.output_id) || !reader.u64(decoded.mode_id) ||
      !reader.u32(decoded.physical_width) ||
      !reader.u32(decoded.physical_height) ||
      !reader.u32(decoded.refresh_millihertz) || !reader.u8(preferred) ||
      !reader.u8(current) || !reader.u16(reserved) ||
      !reader.u32(decoded.flags) || !reader.u32(reserved32))
    return CodecStatus::Truncated;
  decoded.preferred = preferred == 1;
  decoded.current = current == 1;
  const auto status = finish(reader, preferred <= 1 && current <= 1 &&
                                         reserved == 0 && reserved32 == 0 &&
                                         valid_mode(decoded));
  if (status == CodecStatus::Ok) value = decoded;
  return status;
}

std::vector<std::uint8_t> encode(const SurfaceOutputState& value) {
  if (value.output_ids.size() > kMaximumManagedOutputs) return {};
  ByteWriter writer;
  writer.u64(value.surface_id);
  writer.u64(value.primary_output_id);
  writer.u64(value.layout_generation);
  writer.u32(value.preferred_scale_numerator);
  writer.u32(value.preferred_scale_denominator);
  writer.u32(value.client_buffer_scale);
  writer.u16(static_cast<std::uint16_t>(value.scale_mode));
  writer.u16(0);
  writer.u32(value.flags);
  writer.u32(static_cast<std::uint32_t>(value.output_ids.size()));
  for (const auto id : value.output_ids) writer.u64(id);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   SurfaceOutputState& value) {
  ByteReader reader(bytes);
  SurfaceOutputState decoded;
  std::uint32_t count{};
  std::uint16_t mode{}, reserved16{};
  if (!reader.u64(decoded.surface_id) ||
      !reader.u64(decoded.primary_output_id) ||
      !reader.u64(decoded.layout_generation) ||
      !reader.u32(decoded.preferred_scale_numerator) ||
      !reader.u32(decoded.preferred_scale_denominator) ||
      !reader.u32(decoded.client_buffer_scale) || !reader.u16(mode) ||
      !reader.u16(reserved16) || !reader.u32(decoded.flags) ||
      !reader.u32(count))
    return CodecStatus::Truncated;
  if (count > kMaximumManagedOutputs) return CodecStatus::LimitExceeded;
  decoded.output_ids.resize(count);
  for (auto& id : decoded.output_ids)
    if (!reader.u64(id)) return CodecStatus::Truncated;
  decoded.scale_mode = static_cast<SurfaceScaleMode>(mode);
  const auto status =
      finish(reader, reserved16 == 0 && valid_surface_state(decoded));
  if (status == CodecStatus::Ok) value = std::move(decoded);
  return status;
}

std::vector<std::uint8_t> encode(const PolicyOutputUpsert& value) {
  ByteWriter writer;
  writer.u64(value.output_id);
  writer.i32(value.logical_x);
  writer.i32(value.logical_y);
  writer.u32(value.logical_width);
  writer.u32(value.logical_height);
  writer.i32(value.work_x);
  writer.i32(value.work_y);
  writer.u32(value.work_width);
  writer.u32(value.work_height);
  writer.u32(value.scale_numerator);
  writer.u32(value.scale_denominator);
  writer.u16(static_cast<std::uint16_t>(value.transform));
  writer.u8(value.enabled ? 1U : 0U);
  writer.u8(value.primary ? 1U : 0U);
  writer.u32(value.flags);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   PolicyOutputUpsert& value) {
  ByteReader reader(bytes);
  PolicyOutputUpsert decoded;
  std::uint16_t transform{};
  std::uint8_t enabled{}, primary{};
  if (!reader.u64(decoded.output_id) || !reader.i32(decoded.logical_x) ||
      !reader.i32(decoded.logical_y) || !reader.u32(decoded.logical_width) ||
      !reader.u32(decoded.logical_height) || !reader.i32(decoded.work_x) ||
      !reader.i32(decoded.work_y) || !reader.u32(decoded.work_width) ||
      !reader.u32(decoded.work_height) ||
      !reader.u32(decoded.scale_numerator) ||
      !reader.u32(decoded.scale_denominator) || !reader.u16(transform) ||
      !reader.u8(enabled) || !reader.u8(primary) ||
      !reader.u32(decoded.flags))
    return CodecStatus::Truncated;
  decoded.transform = static_cast<Transform>(transform);
  decoded.enabled = enabled == 1;
  decoded.primary = primary == 1;
  const auto status = finish(reader, enabled <= 1 && primary <= 1 &&
                                         valid_policy_output(decoded));
  if (status == CodecStatus::Ok) value = decoded;
  return status;
}

std::vector<std::uint8_t> encode(const PolicyWindowOutputHint& value) {
  ByteWriter writer;
  writer.u32(value.window_id);
  writer.u32(value.flags);
  writer.u64(value.previous_output_id);
  writer.u64(value.preferred_output_id);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   PolicyWindowOutputHint& value) {
  ByteReader reader(bytes);
  PolicyWindowOutputHint decoded;
  if (!reader.u32(decoded.window_id) || !reader.u32(decoded.flags) ||
      !reader.u64(decoded.previous_output_id) ||
      !reader.u64(decoded.preferred_output_id))
    return CodecStatus::Truncated;
  const auto status =
      finish(reader, decoded.window_id != 0 && decoded.flags == 0);
  if (status == CodecStatus::Ok) value = decoded;
  return status;
}

std::vector<std::uint8_t> encode(const OutputStateQuery& value) {
  ByteWriter writer;
  writer.u64(value.query_id);
  writer.u32(value.flags);
  writer.u32(0);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   OutputStateQuery& value) {
  ByteReader reader(bytes);
  OutputStateQuery decoded;
  std::uint32_t reserved{};
  if (!reader.u64(decoded.query_id) || !reader.u32(decoded.flags) ||
      !reader.u32(reserved))
    return CodecStatus::Truncated;
  const auto status = finish(reader, decoded.query_id != 0 &&
                                         decoded.flags != 0 &&
                                         (decoded.flags &
                                          ~kKnownOutputQueryFlags) == 0 &&
                                         reserved == 0);
  if (status == CodecStatus::Ok) value = decoded;
  return status;
}

std::vector<std::uint8_t> encode(const OutputConfigurationCommit& value) {
  ByteWriter writer;
  writer.u64(value.configuration_id);
  writer.u64(value.base_generation);
  writer.u64(value.primary_output_id);
  writer.u32(value.flags);
  writer.u32(0);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   OutputConfigurationCommit& value) {
  ByteReader reader(bytes);
  OutputConfigurationCommit decoded;
  std::uint32_t reserved{};
  if (!reader.u64(decoded.configuration_id) ||
      !reader.u64(decoded.base_generation) ||
      !reader.u64(decoded.primary_output_id) || !reader.u32(decoded.flags) ||
      !reader.u32(reserved))
    return CodecStatus::Truncated;
  const auto status = finish(reader, decoded.configuration_id != 0 &&
                                         decoded.base_generation != 0 &&
                                         decoded.primary_output_id != 0 &&
                                         decoded.flags == 0 && reserved == 0);
  if (status == CodecStatus::Ok) value = decoded;
  return status;
}

std::vector<std::uint8_t> encode(
    const OutputConfigurationAcknowledged& value) {
  ByteWriter writer;
  writer.u64(value.request_id);
  writer.u64(value.applied_generation);
  writer.u16(static_cast<std::uint16_t>(value.result));
  writer.u16(0);
  writer.u32(value.flags);
  writer.u64(value.primary_output_id);
  writer.u32(value.root_logical_width);
  writer.u32(value.root_logical_height);
  writer.u32(value.enabled_output_count);
  writer.u32(0);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   OutputConfigurationAcknowledged& value) {
  ByteReader reader(bytes);
  OutputConfigurationAcknowledged decoded;
  std::uint16_t result{};
  std::uint16_t reserved16{};
  std::uint32_t reserved32{};
  if (!reader.u64(decoded.request_id) ||
      !reader.u64(decoded.applied_generation) || !reader.u16(result) ||
      !reader.u16(reserved16) ||
      !reader.u32(decoded.flags) || !reader.u64(decoded.primary_output_id) ||
      !reader.u32(decoded.root_logical_width) ||
      !reader.u32(decoded.root_logical_height) ||
      !reader.u32(decoded.enabled_output_count) || !reader.u32(reserved32))
    return CodecStatus::Truncated;
  decoded.result = static_cast<OutputConfigurationResult>(result);
  const auto status = finish(
      reader, decoded.request_id != 0 && decoded.applied_generation != 0 &&
                  result >= 1 && result <= 15 && decoded.flags == 0 &&
                  decoded.primary_output_id != 0 &&
                  decoded.root_logical_width != 0 &&
                  decoded.root_logical_width <= 32'767 &&
                  decoded.root_logical_height != 0 &&
                  decoded.root_logical_height <= 32'767 &&
                  decoded.enabled_output_count >= 1 &&
                  decoded.enabled_output_count <= kMaximumManagedOutputs &&
                  reserved16 == 0 && reserved32 == 0);
  if (status == CodecStatus::Ok) value = decoded;
  return status;
}

}  // namespace gw::ipc::wire
