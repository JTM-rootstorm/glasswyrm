#pragma once

#include "ipc/wire/envelope.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gw::ipc::wire {

inline constexpr std::size_t kMaximumInstanceLabel = 64;
inline constexpr std::size_t kMaximumDiagnosticString = 256;
inline constexpr std::uint32_t kHardMaximumPayload = 1024U * 1024U;
inline constexpr std::uint16_t kHardMaximumFds = 16;

struct Hello {
  std::uint16_t minimum_major{kWireMajor};
  std::uint16_t minimum_minor{kWireMinor};
  std::uint16_t maximum_major{kWireMajor};
  std::uint16_t maximum_minor{kWireMinor};
  Role sender_role{Role::Unknown};
  std::uint64_t offered_capabilities{0};
  std::uint64_t required_capabilities{0};
  std::uint32_t maximum_payload{0};
  std::uint16_t maximum_fd_count{0};
  std::array<std::uint8_t, 16> sender_instance_id{};
  std::string name;
};

struct Welcome {
  std::uint16_t selected_major{kWireMajor};
  std::uint16_t selected_minor{kWireMinor};
  Role sender_role{Role::Unknown};
  std::uint64_t negotiated_capabilities{0};
  std::uint32_t negotiated_maximum_payload{0};
  std::uint16_t negotiated_maximum_fd_count{0};
  std::uint64_t connection_id{0};
  std::array<std::uint8_t, 16> sender_instance_id{};
};

struct Reject {
  RejectReason reason{RejectReason::InternalError};
  std::uint16_t supported_minimum_major{kWireMajor};
  std::uint16_t supported_minimum_minor{kWireMinor};
  std::uint16_t supported_maximum_major{kWireMajor};
  std::uint16_t supported_maximum_minor{kWireMinor};
  std::string detail;
};

struct Ping { std::uint64_t nonce{0}; };
struct Pong { std::uint64_t nonce{0}; };

struct ProtocolError {
  ProtocolErrorCode code{ProtocolErrorCode::InternalError};
  MessageType offending_type{MessageType::ProtocolError};
  std::uint64_t offending_sequence{0};
  std::string detail;
};

struct SnapshotBegin {
  std::uint64_t snapshot_id{0};
  SnapshotDomain domain{SnapshotDomain::Test};
  std::uint16_t flags{0};
  std::uint64_t generation{0};
  std::uint32_t expected_item_count{0xffffffffU};
};

struct SnapshotEnd {
  std::uint64_t snapshot_id{0};
  std::uint64_t generation{0};
  std::uint32_t actual_item_count{0};
};

struct SnapshotAbort {
  std::uint64_t snapshot_id{0};
  std::uint16_t reason{0};
  std::string detail;
};

[[nodiscard]] bool valid_utf8(std::string_view value) noexcept;

#define GWIPC_DECLARE_CODEC(Type)                                             \
  [[nodiscard]] std::vector<std::uint8_t> encode(const Type &value);           \
  [[nodiscard]] CodecStatus decode(std::span<const std::uint8_t> bytes,        \
                                   Type &value)

GWIPC_DECLARE_CODEC(Hello);
GWIPC_DECLARE_CODEC(Welcome);
GWIPC_DECLARE_CODEC(Reject);
GWIPC_DECLARE_CODEC(Ping);
GWIPC_DECLARE_CODEC(Pong);
GWIPC_DECLARE_CODEC(ProtocolError);
GWIPC_DECLARE_CODEC(SnapshotBegin);
GWIPC_DECLARE_CODEC(SnapshotEnd);
GWIPC_DECLARE_CODEC(SnapshotAbort);

#undef GWIPC_DECLARE_CODEC

} // namespace gw::ipc::wire
