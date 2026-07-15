#pragma once

#include <cstdint>

namespace gw::ipc::wire {

enum class CodecStatus {
  Ok,
  Truncated,
  TrailingData,
  InvalidValue,
  LimitExceeded,
  SizeMismatch,
};

enum class MessageType : std::uint16_t {
  Hello = 0x0001,
  Welcome = 0x0002,
  Reject = 0x0003,
  Ping = 0x0004,
  Pong = 0x0005,
  ProtocolError = 0x0006,
  SnapshotBegin = 0x0010,
  SnapshotEnd = 0x0011,
  SnapshotAbort = 0x0012,
  OutputUpsert = 0x0100,
  OutputRemove = 0x0101,
  SurfaceUpsert = 0x0110,
  SurfaceRemove = 0x0111,
  SurfacePolicyUpsert = 0x0112,
  BufferAttach = 0x0120,
  BufferDetach = 0x0121,
  BufferRelease = 0x0122,
  SurfaceDamage = 0x0130,
  FrameCommit = 0x0140,
  FrameAcknowledged = 0x0141,
  PolicyContextUpsert = 0x0200,
  PolicyWindowUpsert = 0x0201,
  PolicyWindowRemove = 0x0202,
  PolicyLifecycleWindowUpsert = 0x0203,
  PolicyCommit = 0x0210,
  PolicyWindowState = 0x0211,
  PolicyAcknowledged = 0x0212,
  PolicyBindingsUpsert = 0x0213,
  SyntheticMotion = 0x0300,
  SyntheticButton = 0x0301,
  SyntheticKey = 0x0302,
  SyntheticBarrier = 0x0303,
  SyntheticInputAcknowledged = 0x0310,
  SessionStateChange = 0x0400,
  SessionStateAcknowledged = 0x0401,
};

enum class MessageFlag : std::uint32_t {
  Reply = 1U << 0U,
  Error = 1U << 1U,
  AckRequired = 1U << 2U,
  SnapshotItem = 1U << 3U,
  Critical = 1U << 4U,
};

inline constexpr std::uint32_t kKnownMessageFlags = 0x1fU;

[[nodiscard]] constexpr bool has_flag(const std::uint32_t flags,
                                      const MessageFlag flag) noexcept {
  return (flags & static_cast<std::uint32_t>(flag)) != 0;
}

enum class Role : std::uint16_t {
  Unknown = 0,
  ProtocolServer = 1,
  WindowManager = 2,
  Compositor = 3,
  TestProducer = 4,
  TestConsumer = 5,
  DiagnosticTool = 6,
};

enum class Capability : std::uint64_t {
  FdPassing = 1ULL << 0U,
  Snapshots = 1ULL << 1U,
  OutputState = 1ULL << 2U,
  SurfaceState = 1ULL << 3U,
  MemfdBuffers = 1ULL << 4U,
  DamageRegions = 1ULL << 5U,
  ScaleMetadata = 1ULL << 6U,
  SdrColorMetadata = 1ULL << 7U,
  FrameAcknowledgement = 1ULL << 8U,
  TraceMetadata = 1ULL << 9U,
  WindowPolicy = 1ULL << 10U,
  WindowLifecycle = 1ULL << 11U,
  SyntheticInput = 1ULL << 12U,
  SessionState = 1ULL << 13U,
  InteractivePolicy = 1ULL << 14U,
  CursorSurface = 1ULL << 15U,
};

inline constexpr std::uint64_t kKnownCapabilities = 0xffffULL;

enum class RejectReason : std::uint16_t {
  IncompatibleVersion = 1,
  RoleNotAllowed = 2,
  CapabilityMismatch = 3,
  CredentialRejected = 4,
  InvalidHello = 5,
  ServerBusy = 6,
  InternalError = 7,
};

enum class ProtocolErrorCode : std::uint16_t {
  MalformedEnvelope = 1,
  MalformedPayload = 2,
  UnsupportedMessage = 3,
  MissingCapability = 4,
  InvalidDescriptorCount = 5,
  InvalidDescriptor = 6,
  OutOfOrderSequence = 7,
  UnexpectedReply = 8,
  SnapshotViolation = 9,
  LimitExceeded = 10,
  InternalError = 11,
};

enum class SnapshotDomain : std::uint16_t {
  Outputs = 1,
  Surfaces = 2,
  WindowPolicy = 3,
  CompleteSession = 4,
  Test = 5,
};

} // namespace gw::ipc::wire
