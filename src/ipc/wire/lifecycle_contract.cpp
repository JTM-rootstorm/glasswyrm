#include "ipc/wire/lifecycle_contract.hpp"

#include "ipc/wire/byte_reader.hpp"
#include "ipc/wire/byte_writer.hpp"

namespace gw::ipc::wire {
namespace {

constexpr std::size_t kPolicyWindowUpsertWireSize = 80;

bool tri_state(std::uint16_t value) noexcept { return value <= 2; }

}  // namespace

std::vector<std::uint8_t> encode(const PolicyLifecycleWindowUpsert &value) {
  ByteWriter writer;
  writer.bytes(encode(value.window));
  writer.u64(value.geometry_serial);
  writer.u64(value.stack_serial);
  writer.u32(value.stack_sibling);
  writer.u16(static_cast<std::uint16_t>(value.stack_mode));
  writer.u16(0);
  writer.u32(value.flags);
  writer.u32(0);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   PolicyLifecycleWindowUpsert &value) {
  if (bytes.size() < kPolicyWindowUpsertWireSize)
    return CodecStatus::Truncated;
  PolicyLifecycleWindowUpsert decoded;
  const auto base_status =
      decode(bytes.first(kPolicyWindowUpsertWireSize), decoded.window);
  if (base_status != CodecStatus::Ok) return base_status;
  ByteReader reader(bytes.subspan(kPolicyWindowUpsertWireSize));
  std::uint16_t mode = 0;
  std::uint16_t reserved1 = 0;
  std::uint32_t reserved2 = 0;
  if (!reader.u64(decoded.geometry_serial) ||
      !reader.u64(decoded.stack_serial) ||
      !reader.u32(decoded.stack_sibling) || !reader.u16(mode) ||
      !reader.u16(reserved1) || !reader.u32(decoded.flags) ||
      !reader.u32(reserved2))
    return CodecStatus::Truncated;
  if (!reader.done()) return CodecStatus::TrailingData;
  decoded.stack_mode = static_cast<PolicyStackMode>(mode);
  const bool no_stack = decoded.stack_serial == 0;
  if (reserved1 != 0 || reserved2 != 0 || decoded.flags != 0 ||
      (no_stack && (decoded.stack_sibling != 0 ||
                    decoded.stack_mode != PolicyStackMode::None)) ||
      (!no_stack &&
       (decoded.stack_mode < PolicyStackMode::Above ||
        decoded.stack_mode > PolicyStackMode::Below)) ||
      decoded.stack_sibling == decoded.window.window_id)
    return CodecStatus::InvalidValue;
  value = decoded;
  return CodecStatus::Ok;
}

std::vector<std::uint8_t> encode(const SurfacePolicyUpsert &value) {
  ByteWriter writer;
  writer.u64(value.surface_id);
  writer.u32(value.x11_window_id);
  writer.u32(value.workspace_id);
  writer.u16(static_cast<std::uint16_t>(value.window_type));
  writer.u16(static_cast<std::uint16_t>(value.applied_state));
  writer.u8(value.focused);
  writer.u8(value.managed);
  writer.u8(value.decoration_eligible);
  writer.u8(value.override_redirect);
  writer.u8(value.attention_requested);
  writer.u8(static_cast<std::uint8_t>(value.fullscreen_eligible));
  writer.u8(static_cast<std::uint8_t>(value.direct_scanout_eligible));
  writer.u8(0);
  writer.u32(value.flags);
  writer.u32(0);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   SurfacePolicyUpsert &value) {
  ByteReader reader(bytes);
  SurfacePolicyUpsert decoded;
  std::uint16_t window_type = 0;
  std::uint16_t applied_state = 0;
  std::uint8_t focused = 0, managed = 0, decoration = 0, override = 0;
  std::uint8_t attention = 0, fullscreen = 0, scanout = 0, reserved1 = 0;
  std::uint32_t reserved2 = 0;
  if (!reader.u64(decoded.surface_id) || !reader.u32(decoded.x11_window_id) ||
      !reader.u32(decoded.workspace_id) || !reader.u16(window_type) ||
      !reader.u16(applied_state) || !reader.u8(focused) ||
      !reader.u8(managed) || !reader.u8(decoration) || !reader.u8(override) ||
      !reader.u8(attention) || !reader.u8(fullscreen) || !reader.u8(scanout) ||
      !reader.u8(reserved1) || !reader.u32(decoded.flags) ||
      !reader.u32(reserved2))
    return CodecStatus::Truncated;
  if (!reader.done()) return CodecStatus::TrailingData;
  decoded.window_type = static_cast<PolicyWindowType>(window_type);
  decoded.applied_state = static_cast<PolicyAppliedState>(applied_state);
  decoded.focused = focused;
  decoded.managed = managed;
  decoded.decoration_eligible = decoration;
  decoded.override_redirect = override;
  decoded.attention_requested = attention;
  decoded.fullscreen_eligible = fullscreen;
  decoded.direct_scanout_eligible = scanout;
  if (decoded.surface_id == 0 || decoded.x11_window_id == 0 ||
      decoded.workspace_id == 0 || window_type > 3 || applied_state < 1 ||
      applied_state > 4 || focused > 1 || managed > 1 || decoration > 1 ||
      override > 1 || attention > 1 || !tri_state(fullscreen) ||
      !tri_state(scanout) || reserved1 != 0 || decoded.flags != 0 ||
      reserved2 != 0)
    return CodecStatus::InvalidValue;
  value = decoded;
  return CodecStatus::Ok;
}

}  // namespace gw::ipc::wire
