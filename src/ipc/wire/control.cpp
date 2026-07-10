#include "ipc/wire/control.hpp"

#include "ipc/wire/byte_reader.hpp"
#include "ipc/wire/byte_writer.hpp"

#include <algorithm>
#include <limits>

namespace gw::ipc::wire {
namespace {

template <typename Enum>
[[nodiscard]] bool in_range(const Enum value, const Enum first,
                            const Enum last) noexcept {
  return value >= first && value <= last;
}

[[nodiscard]] bool nonzero(const std::array<std::uint8_t, 16> &id) noexcept {
  return std::any_of(id.begin(), id.end(), [](const auto byte) { return byte != 0; });
}

[[nodiscard]] CodecStatus finished(const ByteReader &reader) noexcept {
  return reader.done() ? CodecStatus::Ok : CodecStatus::TrailingData;
}

[[nodiscard]] bool valid_version_range(const std::uint16_t minimum_major,
                                       const std::uint16_t minimum_minor,
                                       const std::uint16_t maximum_major,
                                       const std::uint16_t maximum_minor) {
  return minimum_major < maximum_major ||
         (minimum_major == maximum_major && minimum_minor <= maximum_minor);
}

} // namespace

bool valid_utf8(const std::string_view value) noexcept {
  std::size_t index = 0;
  while (index < value.size()) {
    const auto first = static_cast<std::uint8_t>(value[index]);
    std::size_t count = 0;
    std::uint32_t codepoint = 0;
    if (first <= 0x7fU) {
      count = 1;
      codepoint = first;
    } else if (first >= 0xc2U && first <= 0xdfU) {
      count = 2;
      codepoint = first & 0x1fU;
    } else if (first >= 0xe0U && first <= 0xefU) {
      count = 3;
      codepoint = first & 0x0fU;
    } else if (first >= 0xf0U && first <= 0xf4U) {
      count = 4;
      codepoint = first & 0x07U;
    } else {
      return false;
    }
    if (value.size() - index < count) {
      return false;
    }
    for (std::size_t continuation = 1; continuation < count; ++continuation) {
      const auto byte = static_cast<std::uint8_t>(value[index + continuation]);
      if ((byte & 0xc0U) != 0x80U) {
        return false;
      }
      codepoint = (codepoint << 6U) | (byte & 0x3fU);
    }
    const bool overlong = (count == 2 && codepoint < 0x80U) ||
                          (count == 3 && codepoint < 0x800U) ||
                          (count == 4 && codepoint < 0x10000U);
    if (overlong || (codepoint >= 0xd800U && codepoint <= 0xdfffU) ||
        codepoint > 0x10ffffU) {
      return false;
    }
    index += count;
  }
  return true;
}

std::vector<std::uint8_t> encode(const Hello &value) {
  if (value.name.size() > kMaximumInstanceLabel) {
    return {};
  }
  ByteWriter writer;
  writer.u16(value.minimum_major);
  writer.u16(value.minimum_minor);
  writer.u16(value.maximum_major);
  writer.u16(value.maximum_minor);
  writer.u16(static_cast<std::uint16_t>(value.sender_role));
  writer.u16(0);
  writer.u64(value.offered_capabilities);
  writer.u64(value.required_capabilities);
  writer.u32(value.maximum_payload);
  writer.u16(value.maximum_fd_count);
  writer.u16(static_cast<std::uint16_t>(value.name.size()));
  writer.bytes(value.sender_instance_id);
  writer.string(value.name);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes, Hello &value) {
  ByteReader reader(bytes);
  Hello decoded;
  std::uint16_t role = 0;
  std::uint16_t reserved = 0;
  std::uint16_t name_size = 0;
  std::span<const std::uint8_t> id;
  if (!reader.u16(decoded.minimum_major) || !reader.u16(decoded.minimum_minor) ||
      !reader.u16(decoded.maximum_major) || !reader.u16(decoded.maximum_minor) ||
      !reader.u16(role) || !reader.u16(reserved) ||
      !reader.u64(decoded.offered_capabilities) ||
      !reader.u64(decoded.required_capabilities) ||
      !reader.u32(decoded.maximum_payload) ||
      !reader.u16(decoded.maximum_fd_count) || !reader.u16(name_size) ||
      !reader.bytes(decoded.sender_instance_id.size(), id) ||
      !reader.string(name_size, decoded.name)) {
    return CodecStatus::Truncated;
  }
  std::copy(id.begin(), id.end(), decoded.sender_instance_id.begin());
  decoded.sender_role = static_cast<Role>(role);
  if (!reader.done()) {
    return CodecStatus::TrailingData;
  }
  if (reserved != 0 ||
      !in_range(decoded.sender_role, Role::ProtocolServer, Role::DiagnosticTool) ||
      !valid_version_range(decoded.minimum_major, decoded.minimum_minor,
                           decoded.maximum_major, decoded.maximum_minor) ||
      !nonzero(decoded.sender_instance_id) ||
      (decoded.required_capabilities & ~kKnownCapabilities) != 0 ||
      decoded.maximum_payload == 0 || decoded.maximum_payload > kHardMaximumPayload ||
      decoded.maximum_fd_count > kHardMaximumFds ||
      decoded.name.size() > kMaximumInstanceLabel || !valid_utf8(decoded.name)) {
    return CodecStatus::InvalidValue;
  }
  value = std::move(decoded);
  return CodecStatus::Ok;
}

std::vector<std::uint8_t> encode(const Welcome &value) {
  ByteWriter writer;
  writer.u16(value.selected_major);
  writer.u16(value.selected_minor);
  writer.u16(static_cast<std::uint16_t>(value.sender_role));
  writer.u16(0);
  writer.u64(value.negotiated_capabilities);
  writer.u32(value.negotiated_maximum_payload);
  writer.u16(value.negotiated_maximum_fd_count);
  writer.u16(0);
  writer.u64(value.connection_id);
  writer.bytes(value.sender_instance_id);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes, Welcome &value) {
  ByteReader reader(bytes);
  Welcome decoded;
  std::uint16_t role = 0;
  std::uint16_t reserved1 = 0;
  std::uint16_t reserved2 = 0;
  std::span<const std::uint8_t> id;
  if (!reader.u16(decoded.selected_major) || !reader.u16(decoded.selected_minor) ||
      !reader.u16(role) || !reader.u16(reserved1) ||
      !reader.u64(decoded.negotiated_capabilities) ||
      !reader.u32(decoded.negotiated_maximum_payload) ||
      !reader.u16(decoded.negotiated_maximum_fd_count) ||
      !reader.u16(reserved2) || !reader.u64(decoded.connection_id) ||
      !reader.bytes(decoded.sender_instance_id.size(), id)) {
    return CodecStatus::Truncated;
  }
  std::copy(id.begin(), id.end(), decoded.sender_instance_id.begin());
  decoded.sender_role = static_cast<Role>(role);
  if (!reader.done()) {
    return CodecStatus::TrailingData;
  }
  if (reserved1 != 0 || reserved2 != 0 || decoded.selected_major != kWireMajor ||
      decoded.selected_minor != kWireMinor ||
      !in_range(decoded.sender_role, Role::ProtocolServer, Role::DiagnosticTool) ||
      (decoded.negotiated_capabilities & ~kKnownCapabilities) != 0 ||
      decoded.negotiated_maximum_payload == 0 ||
      decoded.negotiated_maximum_payload > kHardMaximumPayload ||
      decoded.negotiated_maximum_fd_count > kHardMaximumFds ||
      decoded.connection_id == 0 || !nonzero(decoded.sender_instance_id)) {
    return CodecStatus::InvalidValue;
  }
  value = decoded;
  return CodecStatus::Ok;
}

std::vector<std::uint8_t> encode(const Reject &value) {
  if (value.detail.size() > kMaximumDiagnosticString) {
    return {};
  }
  ByteWriter writer;
  writer.u16(static_cast<std::uint16_t>(value.reason));
  writer.u16(static_cast<std::uint16_t>(value.detail.size()));
  writer.u16(value.supported_minimum_major);
  writer.u16(value.supported_minimum_minor);
  writer.u16(value.supported_maximum_major);
  writer.u16(value.supported_maximum_minor);
  writer.u32(0);
  writer.string(value.detail);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes, Reject &value) {
  ByteReader reader(bytes);
  Reject decoded;
  std::uint16_t reason = 0;
  std::uint16_t detail_size = 0;
  std::uint32_t reserved = 0;
  if (!reader.u16(reason) || !reader.u16(detail_size) ||
      !reader.u16(decoded.supported_minimum_major) ||
      !reader.u16(decoded.supported_minimum_minor) ||
      !reader.u16(decoded.supported_maximum_major) ||
      !reader.u16(decoded.supported_maximum_minor) || !reader.u32(reserved) ||
      !reader.string(detail_size, decoded.detail)) {
    return CodecStatus::Truncated;
  }
  decoded.reason = static_cast<RejectReason>(reason);
  if (!reader.done()) {
    return CodecStatus::TrailingData;
  }
  if (reserved != 0 ||
      !in_range(decoded.reason, RejectReason::IncompatibleVersion,
                RejectReason::InternalError) ||
      !valid_version_range(decoded.supported_minimum_major,
                           decoded.supported_minimum_minor,
                           decoded.supported_maximum_major,
                           decoded.supported_maximum_minor) ||
      decoded.detail.size() > kMaximumDiagnosticString ||
      !valid_utf8(decoded.detail)) {
    return CodecStatus::InvalidValue;
  }
  value = std::move(decoded);
  return CodecStatus::Ok;
}

std::vector<std::uint8_t> encode(const Ping &value) {
  ByteWriter writer;
  writer.u64(value.nonce);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes, Ping &value) {
  ByteReader reader(bytes);
  Ping decoded;
  if (!reader.u64(decoded.nonce)) {
    return CodecStatus::Truncated;
  }
  const auto status = finished(reader);
  if (status == CodecStatus::Ok) {
    value = decoded;
  }
  return status;
}

std::vector<std::uint8_t> encode(const Pong &value) {
  return encode(Ping{value.nonce});
}

CodecStatus decode(const std::span<const std::uint8_t> bytes, Pong &value) {
  Ping ping;
  const auto status = decode(bytes, ping);
  if (status == CodecStatus::Ok) {
    value.nonce = ping.nonce;
  }
  return status;
}

std::vector<std::uint8_t> encode(const ProtocolError &value) {
  if (value.detail.size() > kMaximumDiagnosticString) {
    return {};
  }
  ByteWriter writer;
  writer.u16(static_cast<std::uint16_t>(value.code));
  writer.u16(static_cast<std::uint16_t>(value.offending_type));
  writer.u32(0);
  writer.u64(value.offending_sequence);
  writer.u16(static_cast<std::uint16_t>(value.detail.size()));
  writer.u16(0);
  writer.string(value.detail);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   ProtocolError &value) {
  ByteReader reader(bytes);
  ProtocolError decoded;
  std::uint16_t code = 0;
  std::uint16_t type = 0;
  std::uint32_t reserved1 = 0;
  std::uint16_t detail_size = 0;
  std::uint16_t reserved2 = 0;
  if (!reader.u16(code) || !reader.u16(type) || !reader.u32(reserved1) ||
      !reader.u64(decoded.offending_sequence) || !reader.u16(detail_size) ||
      !reader.u16(reserved2) || !reader.string(detail_size, decoded.detail)) {
    return CodecStatus::Truncated;
  }
  decoded.code = static_cast<ProtocolErrorCode>(code);
  decoded.offending_type = static_cast<MessageType>(type);
  if (!reader.done()) {
    return CodecStatus::TrailingData;
  }
  if (reserved1 != 0 || reserved2 != 0 ||
      !in_range(decoded.code, ProtocolErrorCode::MalformedEnvelope,
                ProtocolErrorCode::InternalError) ||
      decoded.offending_sequence == 0 ||
      decoded.detail.size() > kMaximumDiagnosticString ||
      !valid_utf8(decoded.detail)) {
    return CodecStatus::InvalidValue;
  }
  value = std::move(decoded);
  return CodecStatus::Ok;
}

std::vector<std::uint8_t> encode(const SnapshotBegin &value) {
  ByteWriter writer;
  writer.u64(value.snapshot_id);
  writer.u16(static_cast<std::uint16_t>(value.domain));
  writer.u16(value.flags);
  writer.u64(value.generation);
  writer.u32(value.expected_item_count);
  writer.u32(0);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   SnapshotBegin &value) {
  ByteReader reader(bytes);
  SnapshotBegin decoded;
  std::uint16_t domain = 0;
  std::uint32_t reserved = 0;
  if (!reader.u64(decoded.snapshot_id) || !reader.u16(domain) ||
      !reader.u16(decoded.flags) || !reader.u64(decoded.generation) ||
      !reader.u32(decoded.expected_item_count) || !reader.u32(reserved)) {
    return CodecStatus::Truncated;
  }
  decoded.domain = static_cast<SnapshotDomain>(domain);
  if (!reader.done()) {
    return CodecStatus::TrailingData;
  }
  if (decoded.snapshot_id == 0 || decoded.flags != 0 || reserved != 0 ||
      !in_range(decoded.domain, SnapshotDomain::Outputs, SnapshotDomain::Test)) {
    return CodecStatus::InvalidValue;
  }
  value = decoded;
  return CodecStatus::Ok;
}

std::vector<std::uint8_t> encode(const SnapshotEnd &value) {
  ByteWriter writer;
  writer.u64(value.snapshot_id);
  writer.u64(value.generation);
  writer.u32(value.actual_item_count);
  writer.u32(0);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   SnapshotEnd &value) {
  ByteReader reader(bytes);
  SnapshotEnd decoded;
  std::uint32_t reserved = 0;
  if (!reader.u64(decoded.snapshot_id) || !reader.u64(decoded.generation) ||
      !reader.u32(decoded.actual_item_count) || !reader.u32(reserved)) {
    return CodecStatus::Truncated;
  }
  if (!reader.done()) {
    return CodecStatus::TrailingData;
  }
  if (decoded.snapshot_id == 0 || reserved != 0) {
    return CodecStatus::InvalidValue;
  }
  value = decoded;
  return CodecStatus::Ok;
}

std::vector<std::uint8_t> encode(const SnapshotAbort &value) {
  if (value.detail.size() > kMaximumDiagnosticString) {
    return {};
  }
  ByteWriter writer;
  writer.u64(value.snapshot_id);
  writer.u16(value.reason);
  writer.u16(static_cast<std::uint16_t>(value.detail.size()));
  writer.u32(0);
  writer.string(value.detail);
  return std::move(writer).take();
}

CodecStatus decode(const std::span<const std::uint8_t> bytes,
                   SnapshotAbort &value) {
  ByteReader reader(bytes);
  SnapshotAbort decoded;
  std::uint16_t detail_size = 0;
  std::uint32_t reserved = 0;
  if (!reader.u64(decoded.snapshot_id) || !reader.u16(decoded.reason) ||
      !reader.u16(detail_size) || !reader.u32(reserved) ||
      !reader.string(detail_size, decoded.detail)) {
    return CodecStatus::Truncated;
  }
  if (!reader.done()) {
    return CodecStatus::TrailingData;
  }
  if (decoded.snapshot_id == 0 || decoded.reason == 0 || reserved != 0 ||
      decoded.detail.size() > kMaximumDiagnosticString ||
      !valid_utf8(decoded.detail)) {
    return CodecStatus::InvalidValue;
  }
  value = std::move(decoded);
  return CodecStatus::Ok;
}

} // namespace gw::ipc::wire
