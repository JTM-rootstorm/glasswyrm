#include "ipc/wire/vrr_contract.hpp"

#include "ipc/wire/byte_reader.hpp"
#include "ipc/wire/byte_writer.hpp"

namespace gw::ipc::wire {
namespace {

bool valid_mode(const VrrPolicyMode value) noexcept {
  return value >= VrrPolicyMode::Off && value <= VrrPolicyMode::AlwaysEligible;
}

bool valid_preference(const VrrWindowPreference value) noexcept {
  return value >= VrrWindowPreference::Default &&
         value <= VrrWindowPreference::Prefer;
}

bool valid_decision(const VrrDecision value) noexcept {
  return value >= VrrDecision::Disabled && value <= VrrDecision::Rejected;
}

bool valid_reasons(const std::uint64_t value) noexcept {
  return (value & ~kKnownVrrReasonMask) == 0;
}

CodecStatus finish(const ByteReader &reader, const bool valid) noexcept {
  if (!reader.done())
    return CodecStatus::TrailingData;
  return valid ? CodecStatus::Ok : CodecStatus::InvalidValue;
}

bool read_bool(ByteReader &reader, bool &value, bool &valid) {
  std::uint8_t raw{};
  if (!reader.u8(raw))
    return false;
  value = raw == 1;
  valid = valid && raw <= 1;
  return true;
}

void write_bool(ByteWriter &writer, const bool value) {
  writer.u8(value ? 1U : 0U);
}

bool read_mode(ByteReader &reader, VrrPolicyMode &value, bool &valid) {
  std::uint16_t raw{};
  if (!reader.u16(raw))
    return false;
  value = static_cast<VrrPolicyMode>(raw);
  valid = valid && valid_mode(value);
  return true;
}

bool read_preference(ByteReader &reader, VrrWindowPreference &value,
                     bool &valid) {
  std::uint16_t raw{};
  if (!reader.u16(raw))
    return false;
  value = static_cast<VrrWindowPreference>(raw);
  valid = valid && valid_preference(value);
  return true;
}

bool read_decision(ByteReader &reader, VrrDecision &value, bool &valid) {
  std::uint16_t raw{};
  if (!reader.u16(raw))
    return false;
  value = static_cast<VrrDecision>(raw);
  valid = valid && valid_decision(value);
  return true;
}

} // namespace

std::vector<std::uint8_t> encode(const OutputVrrCapabilityUpsert &value) {
  ByteWriter writer;
  writer.u64(value.output_id);
  write_bool(writer, value.connector_property_present);
  write_bool(writer, value.hardware_capable);
  write_bool(writer, value.kms_controllable);
  write_bool(writer, value.simulated);
  write_bool(writer, value.range_available);
  write_bool(writer, value.atomic_required);
  writer.u16(0);
  writer.u32(value.minimum_refresh_millihertz);
  writer.u32(value.maximum_refresh_millihertz);
  writer.u64(value.reason_flags);
  writer.u32(value.flags);
  writer.u32(0);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   OutputVrrCapabilityUpsert &value) {
  ByteReader reader(bytes);
  OutputVrrCapabilityUpsert decoded;
  std::uint16_t reserved16{};
  std::uint32_t reserved32{};
  bool values_valid = true;
  const bool fields =
      reader.u64(decoded.output_id) &&
      read_bool(reader, decoded.connector_property_present, values_valid) &&
      read_bool(reader, decoded.hardware_capable, values_valid) &&
      read_bool(reader, decoded.kms_controllable, values_valid) &&
      read_bool(reader, decoded.simulated, values_valid) &&
      read_bool(reader, decoded.range_available, values_valid) &&
      read_bool(reader, decoded.atomic_required, values_valid) &&
      reader.u16(reserved16) &&
      reader.u32(decoded.minimum_refresh_millihertz) &&
      reader.u32(decoded.maximum_refresh_millihertz) &&
      reader.u64(decoded.reason_flags) && reader.u32(decoded.flags) &&
      reader.u32(reserved32);
  if (!fields)
    return CodecStatus::Truncated;
  const bool range_valid = decoded.range_available
                               ? decoded.minimum_refresh_millihertz != 0 &&
                                     decoded.minimum_refresh_millihertz <
                                         decoded.maximum_refresh_millihertz
                               : decoded.minimum_refresh_millihertz == 0 &&
                                     decoded.maximum_refresh_millihertz == 0;
  const bool valid =
      values_valid && decoded.output_id != 0 && reserved16 == 0 &&
      reserved32 == 0 && decoded.flags == 0 && range_valid &&
      valid_reasons(decoded.reason_flags) &&
      (!decoded.hardware_capable || decoded.connector_property_present) &&
      (!decoded.simulated || !decoded.hardware_capable);
  const auto status = finish(reader, valid);
  if (status == CodecStatus::Ok)
    value = decoded;
  return status;
}

std::vector<std::uint8_t> encode(const OutputVrrPolicyUpsert &value) {
  ByteWriter writer;
  writer.u64(value.output_id);
  writer.u16(static_cast<std::uint16_t>(value.mode));
  writer.u16(0);
  writer.u32(value.flags);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   OutputVrrPolicyUpsert &value) {
  ByteReader reader(bytes);
  OutputVrrPolicyUpsert decoded;
  std::uint16_t reserved{};
  bool values_valid = true;
  if (!reader.u64(decoded.output_id) ||
      !read_mode(reader, decoded.mode, values_valid) || !reader.u16(reserved) ||
      !reader.u32(decoded.flags))
    return CodecStatus::Truncated;
  const auto status = finish(reader, values_valid && decoded.output_id != 0 &&
                                         reserved == 0 && decoded.flags == 0);
  if (status == CodecStatus::Ok)
    value = decoded;
  return status;
}

std::vector<std::uint8_t> encode(const OutputVrrStateUpsert &value) {
  ByteWriter writer;
  writer.u64(value.output_id);
  writer.u16(static_cast<std::uint16_t>(value.requested_mode));
  writer.u16(static_cast<std::uint16_t>(value.decision));
  write_bool(writer, value.desired_enabled);
  write_bool(writer, value.effective_enabled);
  write_bool(writer, value.property_readback_valid);
  write_bool(writer, value.session_active);
  writer.u32(value.candidate_window_id);
  writer.u32(0);
  writer.u64(value.candidate_surface_id);
  writer.u64(value.reason_flags);
  writer.u64(value.state_generation);
  writer.u64(value.transition_serial);
  writer.u64(value.last_commit_id);
  writer.u64(value.last_presented_generation);
  writer.u32(value.last_flip_sequence);
  writer.u32(value.flags);
  writer.u64(value.last_flip_timestamp_nanoseconds);
  writer.u64(value.last_interval_nanoseconds);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   OutputVrrStateUpsert &value) {
  ByteReader reader(bytes);
  OutputVrrStateUpsert decoded;
  std::uint32_t reserved{};
  bool values_valid = true;
  const bool fields =
      reader.u64(decoded.output_id) &&
      read_mode(reader, decoded.requested_mode, values_valid) &&
      read_decision(reader, decoded.decision, values_valid) &&
      read_bool(reader, decoded.desired_enabled, values_valid) &&
      read_bool(reader, decoded.effective_enabled, values_valid) &&
      read_bool(reader, decoded.property_readback_valid, values_valid) &&
      read_bool(reader, decoded.session_active, values_valid) &&
      reader.u32(decoded.candidate_window_id) && reader.u32(reserved) &&
      reader.u64(decoded.candidate_surface_id) &&
      reader.u64(decoded.reason_flags) &&
      reader.u64(decoded.state_generation) &&
      reader.u64(decoded.transition_serial) &&
      reader.u64(decoded.last_commit_id) &&
      reader.u64(decoded.last_presented_generation) &&
      reader.u32(decoded.last_flip_sequence) && reader.u32(decoded.flags) &&
      reader.u64(decoded.last_flip_timestamp_nanoseconds) &&
      reader.u64(decoded.last_interval_nanoseconds);
  if (!fields)
    return CodecStatus::Truncated;
  const bool simulated =
      (decoded.reason_flags & kVrrReasonSimulatedHeadless) != 0;
  const bool enabled_valid =
      !decoded.effective_enabled ||
      (decoded.desired_enabled && decoded.decision == VrrDecision::Enabled &&
       (decoded.property_readback_valid || simulated));
  const bool decision_reason_valid =
      decoded.decision == VrrDecision::Enabled || decoded.reason_flags != 0;
  const auto status = finish(
      reader, values_valid && decoded.output_id != 0 && reserved == 0 &&
                  decoded.flags == 0 && valid_reasons(decoded.reason_flags) &&
                  decoded.state_generation != 0 && enabled_valid &&
                  decision_reason_valid);
  if (status == CodecStatus::Ok)
    value = decoded;
  return status;
}

std::vector<std::uint8_t> encode(const SurfaceVrrState &value) {
  ByteWriter writer;
  writer.u64(value.surface_id);
  writer.u32(value.window_id);
  writer.u32(0);
  writer.u64(value.output_id);
  writer.u16(static_cast<std::uint16_t>(value.preference));
  write_bool(writer, value.policy_selected);
  write_bool(writer, value.policy_eligible);
  write_bool(writer, value.focused);
  write_bool(writer, value.fullscreen);
  write_bool(writer, value.borderless_fullscreen);
  write_bool(writer, value.exclusive_output_membership);
  writer.u64(value.reason_flags);
  writer.u64(value.policy_generation);
  writer.u32(value.flags);
  writer.u32(0);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   SurfaceVrrState &value) {
  ByteReader reader(bytes);
  SurfaceVrrState decoded;
  std::uint32_t reserved{}, trailing{};
  bool values_valid = true;
  const bool fields =
      reader.u64(decoded.surface_id) && reader.u32(decoded.window_id) &&
      reader.u32(reserved) && reader.u64(decoded.output_id) &&
      read_preference(reader, decoded.preference, values_valid) &&
      read_bool(reader, decoded.policy_selected, values_valid) &&
      read_bool(reader, decoded.policy_eligible, values_valid) &&
      read_bool(reader, decoded.focused, values_valid) &&
      read_bool(reader, decoded.fullscreen, values_valid) &&
      read_bool(reader, decoded.borderless_fullscreen, values_valid) &&
      read_bool(reader, decoded.exclusive_output_membership, values_valid) &&
      reader.u64(decoded.reason_flags) &&
      reader.u64(decoded.policy_generation) && reader.u32(decoded.flags) &&
      reader.u32(trailing);
  if (!fields)
    return CodecStatus::Truncated;
  const auto status =
      finish(reader, values_valid && decoded.surface_id != 0 &&
                         decoded.window_id != 0 && decoded.output_id != 0 &&
                         reserved == 0 && trailing == 0 && decoded.flags == 0 &&
                         valid_reasons(decoded.reason_flags) &&
                         decoded.policy_generation != 0 &&
                         (!decoded.policy_selected || decoded.policy_eligible));
  if (status == CodecStatus::Ok)
    value = decoded;
  return status;
}

std::vector<std::uint8_t> encode(const PolicyWindowVrrUpsert &value) {
  ByteWriter writer;
  writer.u32(value.window_id);
  writer.u16(static_cast<std::uint16_t>(value.preference));
  writer.u16(0);
  writer.u32(value.flags);
  writer.u32(0);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   PolicyWindowVrrUpsert &value) {
  ByteReader reader(bytes);
  PolicyWindowVrrUpsert decoded;
  std::uint16_t reserved16{};
  std::uint32_t reserved32{};
  bool values_valid = true;
  if (!reader.u32(decoded.window_id) ||
      !read_preference(reader, decoded.preference, values_valid) ||
      !reader.u16(reserved16) || !reader.u32(decoded.flags) ||
      !reader.u32(reserved32))
    return CodecStatus::Truncated;
  const auto status = finish(reader, values_valid && decoded.window_id != 0 &&
                                         reserved16 == 0 && reserved32 == 0 &&
                                         decoded.flags == 0);
  if (status == CodecStatus::Ok)
    value = decoded;
  return status;
}

std::vector<std::uint8_t> encode(const PolicyOutputVrrUpsert &value) {
  ByteWriter writer;
  writer.u64(value.output_id);
  writer.u16(static_cast<std::uint16_t>(value.mode));
  write_bool(writer, value.hardware_capable);
  write_bool(writer, value.kms_controllable);
  writer.u32(value.flags);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   PolicyOutputVrrUpsert &value) {
  ByteReader reader(bytes);
  PolicyOutputVrrUpsert decoded;
  bool values_valid = true;
  if (!reader.u64(decoded.output_id) ||
      !read_mode(reader, decoded.mode, values_valid) ||
      !read_bool(reader, decoded.hardware_capable, values_valid) ||
      !read_bool(reader, decoded.kms_controllable, values_valid) ||
      !reader.u32(decoded.flags))
    return CodecStatus::Truncated;
  const auto status = finish(reader, values_valid && decoded.output_id != 0 &&
                                         decoded.flags == 0);
  if (status == CodecStatus::Ok)
    value = decoded;
  return status;
}

std::vector<std::uint8_t> encode(const PolicyWindowVrrState &value) {
  ByteWriter writer;
  writer.u32(value.window_id);
  writer.u32(0);
  writer.u64(value.output_id);
  writer.u16(static_cast<std::uint16_t>(value.preference));
  write_bool(writer, value.selected);
  write_bool(writer, value.eligible);
  write_bool(writer, value.focused);
  write_bool(writer, value.fullscreen);
  write_bool(writer, value.borderless_fullscreen);
  write_bool(writer, value.exclusive_output_membership);
  writer.u64(value.reason_flags);
  writer.u32(value.flags);
  writer.u32(0);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   PolicyWindowVrrState &value) {
  ByteReader reader(bytes);
  PolicyWindowVrrState decoded;
  std::uint32_t reserved{}, trailing{};
  bool values_valid = true;
  const bool fields =
      reader.u32(decoded.window_id) && reader.u32(reserved) &&
      reader.u64(decoded.output_id) &&
      read_preference(reader, decoded.preference, values_valid) &&
      read_bool(reader, decoded.selected, values_valid) &&
      read_bool(reader, decoded.eligible, values_valid) &&
      read_bool(reader, decoded.focused, values_valid) &&
      read_bool(reader, decoded.fullscreen, values_valid) &&
      read_bool(reader, decoded.borderless_fullscreen, values_valid) &&
      read_bool(reader, decoded.exclusive_output_membership, values_valid) &&
      reader.u64(decoded.reason_flags) && reader.u32(decoded.flags) &&
      reader.u32(trailing);
  if (!fields)
    return CodecStatus::Truncated;
  const auto status = finish(
      reader, values_valid && decoded.window_id != 0 &&
                  decoded.output_id != 0 && reserved == 0 && trailing == 0 &&
                  decoded.flags == 0 && valid_reasons(decoded.reason_flags) &&
                  (!decoded.selected || decoded.eligible));
  if (status == CodecStatus::Ok)
    value = decoded;
  return status;
}

std::vector<std::uint8_t> encode(const PolicyOutputVrrState &value) {
  ByteWriter writer;
  writer.u64(value.output_id);
  writer.u16(static_cast<std::uint16_t>(value.mode));
  write_bool(writer, value.desired_enabled);
  write_bool(writer, value.candidate_required);
  writer.u32(value.selected_window_id);
  writer.u64(value.reason_flags);
  writer.u32(value.flags);
  writer.u32(0);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   PolicyOutputVrrState &value) {
  ByteReader reader(bytes);
  PolicyOutputVrrState decoded;
  std::uint32_t reserved{};
  bool values_valid = true;
  const bool fields =
      reader.u64(decoded.output_id) &&
      read_mode(reader, decoded.mode, values_valid) &&
      read_bool(reader, decoded.desired_enabled, values_valid) &&
      read_bool(reader, decoded.candidate_required, values_valid) &&
      reader.u32(decoded.selected_window_id) &&
      reader.u64(decoded.reason_flags) && reader.u32(decoded.flags) &&
      reader.u32(reserved);
  if (!fields)
    return CodecStatus::Truncated;
  const auto status =
      finish(reader,
             values_valid && decoded.output_id != 0 && reserved == 0 &&
                 decoded.flags == 0 && valid_reasons(decoded.reason_flags) &&
                 (!decoded.candidate_required ||
                  decoded.selected_window_id != 0 || !decoded.desired_enabled));
  if (status == CodecStatus::Ok)
    value = decoded;
  return status;
}

std::vector<std::uint8_t> encode(const PresentationTiming &value) {
  ByteWriter writer;
  writer.u64(value.output_id);
  writer.u64(value.commit_id);
  writer.u64(value.presented_generation);
  writer.u32(value.flip_sequence);
  writer.u32(value.flags);
  writer.u64(value.kernel_timestamp_nanoseconds);
  writer.u64(value.interval_nanoseconds);
  write_bool(writer, value.effective_vrr_enabled);
  write_bool(writer, value.timestamp_available);
  writer.u16(0);
  writer.u32(0);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   PresentationTiming &value) {
  ByteReader reader(bytes);
  PresentationTiming decoded;
  std::uint16_t reserved16{};
  std::uint32_t reserved32{};
  bool values_valid = true;
  const bool fields =
      reader.u64(decoded.output_id) && reader.u64(decoded.commit_id) &&
      reader.u64(decoded.presented_generation) &&
      reader.u32(decoded.flip_sequence) && reader.u32(decoded.flags) &&
      reader.u64(decoded.kernel_timestamp_nanoseconds) &&
      reader.u64(decoded.interval_nanoseconds) &&
      read_bool(reader, decoded.effective_vrr_enabled, values_valid) &&
      read_bool(reader, decoded.timestamp_available, values_valid) &&
      reader.u16(reserved16) && reader.u32(reserved32);
  if (!fields)
    return CodecStatus::Truncated;
  const bool timing_valid = decoded.timestamp_available
                                ? decoded.kernel_timestamp_nanoseconds != 0
                                : decoded.kernel_timestamp_nanoseconds == 0 &&
                                      decoded.interval_nanoseconds == 0;
  const auto status = finish(
      reader, values_valid && decoded.output_id != 0 &&
                  decoded.commit_id != 0 && decoded.presented_generation != 0 &&
                  (decoded.flags & ~kPresentationTimingSimulated) == 0 &&
                  reserved16 == 0 && reserved32 == 0 && timing_valid);
  if (status == CodecStatus::Ok)
    value = decoded;
  return status;
}

} // namespace gw::ipc::wire
