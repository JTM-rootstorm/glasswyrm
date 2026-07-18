#include "ipc/connection_internal.hpp"

#include "ipc/wire/compositor_contract.hpp"
#include "ipc/wire/control.hpp"
#include "ipc/wire/input_contract.hpp"
#include "ipc/wire/lifecycle_contract.hpp"
#include "ipc/wire/policy_contract.hpp"
#include "ipc/wire/session_contract.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <utility>

namespace gw::ipc {

bool output_extension_message(std::uint16_t type) noexcept;
gwipc_status validate_output_extension(
    const gwipc_connection& connection, std::uint16_t type,
    std::uint32_t flags, std::span<const std::uint8_t> payload,
    std::span<const int> fds, const SnapshotState& snapshot,
    MessageDirection direction);

namespace {

std::uint64_t required_capability(std::uint16_t type) noexcept {
  switch (type) {
    case GWIPC_MESSAGE_SNAPSHOT_BEGIN:
    case GWIPC_MESSAGE_SNAPSHOT_END:
    case GWIPC_MESSAGE_SNAPSHOT_ABORT:
      return GWIPC_CAP_SNAPSHOTS;
    case GWIPC_MESSAGE_OUTPUT_UPSERT:
    case GWIPC_MESSAGE_OUTPUT_REMOVE:
      return GWIPC_CAP_OUTPUT_STATE;
    case GWIPC_MESSAGE_SURFACE_UPSERT:
    case GWIPC_MESSAGE_SURFACE_REMOVE:
      return GWIPC_CAP_SURFACE_STATE;
    case GWIPC_MESSAGE_SURFACE_POLICY_UPSERT:
      return GWIPC_CAP_SURFACE_STATE | GWIPC_CAP_WINDOW_LIFECYCLE;
    case GWIPC_MESSAGE_BUFFER_ATTACH:
      return GWIPC_CAP_FD_PASSING | GWIPC_CAP_MEMFD_BUFFERS;
    case GWIPC_MESSAGE_SURFACE_DAMAGE:
      return GWIPC_CAP_DAMAGE_REGIONS;
    case GWIPC_MESSAGE_FRAME_ACKNOWLEDGED:
      return GWIPC_CAP_FRAME_ACKNOWLEDGEMENT;
    case GWIPC_MESSAGE_POLICY_CONTEXT_UPSERT:
    case GWIPC_MESSAGE_POLICY_WINDOW_UPSERT:
    case GWIPC_MESSAGE_POLICY_WINDOW_REMOVE:
    case GWIPC_MESSAGE_POLICY_COMMIT:
    case GWIPC_MESSAGE_POLICY_WINDOW_STATE:
    case GWIPC_MESSAGE_POLICY_ACKNOWLEDGED:
      return GWIPC_CAP_WINDOW_POLICY;
    case GWIPC_MESSAGE_POLICY_LIFECYCLE_WINDOW_UPSERT:
      return GWIPC_CAP_WINDOW_POLICY | GWIPC_CAP_WINDOW_LIFECYCLE;
    case GWIPC_MESSAGE_SYNTHETIC_MOTION:
    case GWIPC_MESSAGE_SYNTHETIC_BUTTON:
    case GWIPC_MESSAGE_SYNTHETIC_KEY:
    case GWIPC_MESSAGE_SYNTHETIC_BARRIER:
    case GWIPC_MESSAGE_SYNTHETIC_INPUT_ACKNOWLEDGED:
      return GWIPC_CAP_SYNTHETIC_INPUT;
    case GWIPC_MESSAGE_POLICY_BINDINGS_UPSERT:
      return GWIPC_CAP_WINDOW_POLICY | GWIPC_CAP_INTERACTIVE_POLICY;
    case GWIPC_MESSAGE_SESSION_STATE_CHANGE:
    case GWIPC_MESSAGE_SESSION_STATE_ACKNOWLEDGED:
      return GWIPC_CAP_SESSION_STATE;
    default:
      return 0;
  }
}

gwipc_status validate_snapshot(SnapshotState& state, std::uint16_t type,
                               std::uint32_t flags,
                               std::span<const std::uint8_t> payload,
                               bool require_policy_bindings) {
  const bool item = (flags & GWIPC_FLAG_SNAPSHOT_ITEM) != 0;
  if (snapshot_control(type) && item) return GWIPC_STATUS_PROTOCOL_ERROR;
  if (type == GWIPC_MESSAGE_SNAPSHOT_BEGIN) {
    wire::SnapshotBegin begin;
    if (state.active || wire::decode(payload, begin) != wire::CodecStatus::Ok)
      return GWIPC_STATUS_PROTOCOL_ERROR;
    state = {true, begin.snapshot_id, begin.generation,
             begin.expected_item_count, 0,
             static_cast<std::uint16_t>(begin.domain), 0};
    return GWIPC_STATUS_OK;
  }
  if (type == GWIPC_MESSAGE_SNAPSHOT_END) {
    wire::SnapshotEnd end;
    if (!state.active || wire::decode(payload, end) != wire::CodecStatus::Ok ||
        end.snapshot_id != state.id || end.generation != state.generation ||
        end.actual_item_count != state.item_count ||
        (state.expected_count != UINT32_MAX &&
         state.expected_count != state.item_count) ||
        (require_policy_bindings &&
         state.domain ==
             static_cast<std::uint16_t>(wire::SnapshotDomain::WindowPolicy) &&
         state.policy_bindings_count != 1))
      return GWIPC_STATUS_PROTOCOL_ERROR;
    state = {};
    return GWIPC_STATUS_OK;
  }
  if (type == GWIPC_MESSAGE_POLICY_BINDINGS_UPSERT) {
    if (!state.active ||
        state.domain !=
            static_cast<std::uint16_t>(wire::SnapshotDomain::WindowPolicy) ||
        state.policy_bindings_count != 0)
      return GWIPC_STATUS_PROTOCOL_ERROR;
    ++state.policy_bindings_count;
  }
  if (type == GWIPC_MESSAGE_SNAPSHOT_ABORT) {
    wire::SnapshotAbort abort;
    if (!state.active || wire::decode(payload, abort) != wire::CodecStatus::Ok ||
        abort.snapshot_id != state.id)
      return GWIPC_STATUS_PROTOCOL_ERROR;
    state = {};
    return GWIPC_STATUS_OK;
  }
  if (item) {
    if (!state.active || state.item_count == UINT32_MAX)
      return GWIPC_STATUS_PROTOCOL_ERROR;
    ++state.item_count;
  }
  return GWIPC_STATUS_OK;
}

bool is_eventfd(const int fd) noexcept {
  std::array<char, 64> path{};
  const int length = std::snprintf(path.data(), path.size(),
                                   "/proc/self/fd/%d", fd);
  if (length <= 0 || static_cast<std::size_t>(length) >= path.size())
    return false;
  std::array<char, 64> target{};
  const auto count = ::readlink(path.data(), target.data(), target.size() - 1);
  return count == static_cast<ssize_t>(std::strlen("anon_inode:[eventfd]")) &&
         std::memcmp(target.data(), "anon_inode:[eventfd]",
                     static_cast<std::size_t>(count)) == 0;
}

gwipc_status validate_buffer_attach(const gwipc_connection& connection,
                                    std::span<const std::uint8_t> payload,
                                    std::span<const int> fds,
                                    wire::CodecStatus& codec) {
  wire::BufferAttach value;
  codec = wire::decode(payload, value);
  if (codec != wire::CodecStatus::Ok) return GWIPC_STATUS_OK;
  const bool synchronized =
      value.synchronization == wire::SynchronizationMode::EventFd;
  if (synchronized &&
      (connection.peer.capabilities &
       GWIPC_CAP_CPU_BUFFER_SYNCHRONIZATION) == 0)
    return GWIPC_STATUS_CAPABILITY_MISMATCH;
  const std::size_t expected_fds = synchronized ? 2U : 1U;
  if (fds.size() != expected_fds) return GWIPC_STATUS_OK;
  struct stat status {};
  const int descriptor_flags = ::fcntl(fds[0], F_GETFL);
  if (descriptor_flags < 0 || (descriptor_flags & O_PATH) != 0 ||
      (descriptor_flags & O_ACCMODE) == O_WRONLY ||
      ::fstat(fds[0], &status) < 0 || !S_ISREG(status.st_mode) ||
      status.st_size < 0 ||
      static_cast<std::uint64_t>(status.st_size) < value.storage_size)
    return GWIPC_STATUS_PROTOCOL_ERROR;
  if (synchronized) {
    const int status_flags = ::fcntl(fds[1], F_GETFL);
    const int descriptor_status = ::fcntl(fds[1], F_GETFD);
    if (status_flags < 0 || (status_flags & O_NONBLOCK) == 0 ||
        descriptor_status < 0 || (descriptor_status & FD_CLOEXEC) == 0 ||
        !is_eventfd(fds[1]))
      return GWIPC_STATUS_PROTOCOL_ERROR;
  }
  return GWIPC_STATUS_OK;
}

gwipc_status validate_control(std::uint16_t type, std::uint32_t flags,
                              std::span<const std::uint8_t> payload,
                              wire::CodecStatus& codec) {
  switch (type) {
    case GWIPC_MESSAGE_PING: {
      wire::Ping value;
      codec = wire::decode(payload, value);
      return flags == GWIPC_FLAG_ACK_REQUIRED ? GWIPC_STATUS_OK
                                               : GWIPC_STATUS_PROTOCOL_ERROR;
    }
    case GWIPC_MESSAGE_PONG: {
      wire::Pong value;
      codec = wire::decode(payload, value);
      return flags == GWIPC_FLAG_REPLY ? GWIPC_STATUS_OK
                                       : GWIPC_STATUS_PROTOCOL_ERROR;
    }
    case GWIPC_MESSAGE_PROTOCOL_ERROR: {
      wire::ProtocolError value;
      codec = wire::decode(payload, value);
      return flags == (GWIPC_FLAG_REPLY | GWIPC_FLAG_ERROR)
                 ? GWIPC_STATUS_OK
                 : GWIPC_STATUS_PROTOCOL_ERROR;
    }
    case GWIPC_MESSAGE_SNAPSHOT_BEGIN: {
      wire::SnapshotBegin value;
      codec = wire::decode(payload, value);
      return GWIPC_STATUS_OK;
    }
    case GWIPC_MESSAGE_SNAPSHOT_END: {
      wire::SnapshotEnd value;
      codec = wire::decode(payload, value);
      return GWIPC_STATUS_OK;
    }
    default: {
      wire::SnapshotAbort value;
      codec = wire::decode(payload, value);
      return GWIPC_STATUS_OK;
    }
  }
}

gwipc_status validate_compositor(gwipc_connection& connection,
                                 std::uint16_t type, std::uint32_t flags,
                                 std::span<const std::uint8_t> payload,
                                 std::span<const int> fds,
                                 wire::CodecStatus& codec) {
  switch (type) {
    case GWIPC_MESSAGE_OUTPUT_UPSERT: { wire::OutputUpsert v; codec=wire::decode(payload,v); break; }
    case GWIPC_MESSAGE_OUTPUT_REMOVE: { wire::OutputRemove v; codec=wire::decode(payload,v); break; }
    case GWIPC_MESSAGE_SURFACE_UPSERT: {
      wire::SurfaceUpsert v; codec=wire::decode(payload,v);
      if (codec == wire::CodecStatus::Ok &&
          (v.presentation_flags & GWIPC_SURFACE_PRESENTATION_METADATA_ONLY) != 0 &&
          (connection.peer.capabilities & GWIPC_CAP_WINDOW_LIFECYCLE) == 0)
        return GWIPC_STATUS_CAPABILITY_MISMATCH;
      if (codec == wire::CodecStatus::Ok &&
          (v.presentation_flags & GWIPC_SURFACE_PRESENTATION_CURSOR) != 0) {
        constexpr std::uint64_t required =
            GWIPC_CAP_CURSOR_SURFACE | GWIPC_CAP_FD_PASSING |
            GWIPC_CAP_MEMFD_BUFFERS | GWIPC_CAP_DAMAGE_REGIONS |
            GWIPC_CAP_WINDOW_LIFECYCLE;
        if ((connection.peer.capabilities & required) != required)
          return GWIPC_STATUS_CAPABILITY_MISMATCH;
      }
      break;
    }
    case GWIPC_MESSAGE_SURFACE_REMOVE: { wire::SurfaceRemove v; codec=wire::decode(payload,v); break; }
    case GWIPC_MESSAGE_BUFFER_ATTACH:
      return validate_buffer_attach(connection, payload, fds, codec);
    case GWIPC_MESSAGE_BUFFER_DETACH: { wire::BufferDetach v; codec=wire::decode(payload,v); break; }
    case GWIPC_MESSAGE_BUFFER_RELEASE: { wire::BufferRelease v; codec=wire::decode(payload,v); break; }
    case GWIPC_MESSAGE_SURFACE_DAMAGE: { wire::SurfaceDamage v; codec=wire::decode(payload,v); break; }
    case GWIPC_MESSAGE_FRAME_COMMIT: {
      wire::FrameCommit v; codec=wire::decode(payload,v);
      if (flags != GWIPC_FLAG_ACK_REQUIRED) return GWIPC_STATUS_PROTOCOL_ERROR;
      break;
    }
    default: {
      wire::FrameAcknowledged v; codec=wire::decode(payload,v);
      if (flags != GWIPC_FLAG_REPLY) return GWIPC_STATUS_PROTOCOL_ERROR;
    }
  }
  return GWIPC_STATUS_OK;
}

gwipc_status validate_policy(std::uint16_t type, std::uint32_t flags,
                             std::span<const std::uint8_t> payload,
                             const SnapshotState& snapshot,
                             wire::CodecStatus& codec) {
  switch (type) {
    case GWIPC_MESSAGE_POLICY_CONTEXT_UPSERT: { wire::PolicyContextUpsert v; codec=wire::decode(payload,v); if(flags!=0&&flags!=GWIPC_FLAG_SNAPSHOT_ITEM)return GWIPC_STATUS_PROTOCOL_ERROR; break; }
    case GWIPC_MESSAGE_POLICY_WINDOW_UPSERT: { wire::PolicyWindowUpsert v; codec=wire::decode(payload,v); if(flags!=0&&flags!=GWIPC_FLAG_SNAPSHOT_ITEM)return GWIPC_STATUS_PROTOCOL_ERROR; break; }
    case GWIPC_MESSAGE_POLICY_WINDOW_REMOVE: { wire::PolicyWindowRemove v; codec=wire::decode(payload,v); if(flags!=0)return GWIPC_STATUS_PROTOCOL_ERROR; break; }
    case GWIPC_MESSAGE_POLICY_COMMIT: { wire::PolicyCommit v; codec=wire::decode(payload,v); if(flags!=GWIPC_FLAG_ACK_REQUIRED||snapshot.active)return GWIPC_STATUS_PROTOCOL_ERROR; break; }
    case GWIPC_MESSAGE_POLICY_WINDOW_STATE: { wire::PolicyWindowState v; codec=wire::decode(payload,v); if(flags!=GWIPC_FLAG_SNAPSHOT_ITEM)return GWIPC_STATUS_PROTOCOL_ERROR; break; }
    case GWIPC_MESSAGE_POLICY_ACKNOWLEDGED: { wire::PolicyAcknowledged v; codec=wire::decode(payload,v); if(flags!=GWIPC_FLAG_REPLY)return GWIPC_STATUS_PROTOCOL_ERROR; break; }
    case GWIPC_MESSAGE_POLICY_LIFECYCLE_WINDOW_UPSERT: { wire::PolicyLifecycleWindowUpsert v; codec=wire::decode(payload,v); if(flags!=0&&flags!=GWIPC_FLAG_SNAPSHOT_ITEM)return GWIPC_STATUS_PROTOCOL_ERROR; break; }
    case GWIPC_MESSAGE_POLICY_BINDINGS_UPSERT: { wire::PolicyBindingsUpsert v; codec=wire::decode(payload,v); if(flags!=GWIPC_FLAG_SNAPSHOT_ITEM)return GWIPC_STATUS_PROTOCOL_ERROR; break; }
    default: { wire::SurfacePolicyUpsert v; codec=wire::decode(payload,v); if(flags!=GWIPC_FLAG_SNAPSHOT_ITEM)return GWIPC_STATUS_PROTOCOL_ERROR; }
  }
  return GWIPC_STATUS_OK;
}

gwipc_status validate_session(std::uint16_t type, std::uint32_t flags,
                              std::span<const std::uint8_t> payload,
                              wire::CodecStatus& codec) {
  if (type == GWIPC_MESSAGE_SESSION_STATE_CHANGE) {
    wire::SessionStateChange value;
    codec = wire::decode(payload, value);
    return flags == GWIPC_FLAG_ACK_REQUIRED ? GWIPC_STATUS_OK
                                             : GWIPC_STATUS_PROTOCOL_ERROR;
  }
  wire::SessionStateAcknowledged value;
  codec = wire::decode(payload, value);
  return flags == GWIPC_FLAG_REPLY ? GWIPC_STATUS_OK
                                   : GWIPC_STATUS_PROTOCOL_ERROR;
}

bool valid_direction(const gwipc_connection& connection, std::uint16_t type,
                     MessageDirection direction) noexcept {
  gwipc_role sender = connection.config.local_role;
  gwipc_role receiver = connection.peer.role;
  if (direction == MessageDirection::Incoming)
    std::swap(sender, receiver);
  if (type == GWIPC_MESSAGE_SESSION_STATE_CHANGE)
    return sender == GWIPC_ROLE_COMPOSITOR &&
           receiver == GWIPC_ROLE_PROTOCOL_SERVER;
  if (type == GWIPC_MESSAGE_SESSION_STATE_ACKNOWLEDGED)
    return sender == GWIPC_ROLE_PROTOCOL_SERVER &&
           receiver == GWIPC_ROLE_COMPOSITOR;
  if (type == GWIPC_MESSAGE_POLICY_BINDINGS_UPSERT)
    return sender == GWIPC_ROLE_WINDOW_MANAGER &&
           receiver == GWIPC_ROLE_PROTOCOL_SERVER;
  return true;
}

bool policy_output_direction(const gwipc_connection& connection,
                             MessageDirection direction) noexcept {
  gwipc_role sender = connection.config.local_role;
  gwipc_role receiver = connection.peer.role;
  if (direction == MessageDirection::Incoming)
    std::swap(sender, receiver);
  return sender == GWIPC_ROLE_WINDOW_MANAGER &&
         receiver == GWIPC_ROLE_PROTOCOL_SERVER;
}

bool valid_snapshot_begin_domain(const gwipc_connection& connection,
                                 const std::span<const std::uint8_t> payload,
                                 const MessageDirection direction) noexcept {
  wire::SnapshotBegin begin;
  if (wire::decode(payload, begin) != wire::CodecStatus::Ok) return false;
  auto sender = connection.config.local_role;
  auto receiver = connection.peer.role;
  if (direction == MessageDirection::Incoming) std::swap(sender, receiver);
  if (sender == GWIPC_ROLE_PROTOCOL_SERVER &&
      receiver == GWIPC_ROLE_COMPOSITOR)
    return begin.domain == wire::SnapshotDomain::CompleteSession;
  if (sender == GWIPC_ROLE_COMPOSITOR &&
      receiver == GWIPC_ROLE_PROTOCOL_SERVER)
    return begin.domain == wire::SnapshotDomain::Outputs;
  return true;
}

gwipc_status validate_input(std::uint16_t type, std::uint32_t flags,
                            std::span<const std::uint8_t> payload,
                            wire::CodecStatus& codec) {
  switch (type) {
    case GWIPC_MESSAGE_SYNTHETIC_MOTION: { wire::SyntheticMotion v; codec=wire::decode(payload,v); break; }
    case GWIPC_MESSAGE_SYNTHETIC_BUTTON: { wire::SyntheticButton v; codec=wire::decode(payload,v); break; }
    case GWIPC_MESSAGE_SYNTHETIC_KEY: { wire::SyntheticKey v; codec=wire::decode(payload,v); break; }
    case GWIPC_MESSAGE_SYNTHETIC_BARRIER: { wire::SyntheticBarrier v; codec=wire::decode(payload,v); break; }
    default: { wire::SyntheticInputAcknowledged v; codec=wire::decode(payload,v); return flags==GWIPC_FLAG_REPLY?GWIPC_STATUS_OK:GWIPC_STATUS_PROTOCOL_ERROR; }
  }
  return flags == GWIPC_FLAG_ACK_REQUIRED ? GWIPC_STATUS_OK
                                           : GWIPC_STATUS_PROTOCOL_ERROR;
}

}  // namespace

bool snapshot_control(std::uint16_t type) noexcept {
  return type >= GWIPC_MESSAGE_SNAPSHOT_BEGIN &&
         type <= GWIPC_MESSAGE_SNAPSHOT_ABORT;
}

gwipc_status validate_application(gwipc_connection& connection,
                                  std::uint16_t type, std::uint32_t flags,
                                  std::span<const std::uint8_t> payload,
                                  std::span<const int> fds,
                                  SnapshotState& snapshot,
                                  MessageDirection direction) {
  if (type == GWIPC_MESSAGE_SNAPSHOT_BEGIN &&
      !valid_snapshot_begin_domain(connection, payload, direction))
    return GWIPC_STATUS_PROTOCOL_ERROR;
  if (output_extension_message(type)) {
    const auto status = validate_output_extension(
        connection, type, flags, payload, fds, snapshot, direction);
    if (status != GWIPC_STATUS_OK) return status;
    return validate_snapshot(
        snapshot, type, flags, payload,
        (connection.peer.capabilities & GWIPC_CAP_INTERACTIVE_POLICY) != 0 &&
            policy_output_direction(connection, direction));
  }
  const auto required = required_capability(type);
  if ((required & connection.peer.capabilities) != required)
    return GWIPC_STATUS_CAPABILITY_MISMATCH;
  if (!valid_direction(connection, type, direction))
    return GWIPC_STATUS_PROTOCOL_ERROR;
  wire::CodecStatus codec = wire::CodecStatus::InvalidValue;
  gwipc_status status = GWIPC_STATUS_PROTOCOL_ERROR;
  if ((type >= GWIPC_MESSAGE_PING && type <= GWIPC_MESSAGE_PROTOCOL_ERROR) ||
      snapshot_control(type))
    status = validate_control(type, flags, payload, codec);
  else if (type == GWIPC_MESSAGE_SURFACE_POLICY_UPSERT ||
           (type >= GWIPC_MESSAGE_POLICY_CONTEXT_UPSERT &&
            type <= GWIPC_MESSAGE_POLICY_BINDINGS_UPSERT))
    status = validate_policy(type, flags, payload, snapshot, codec);
  else if (type >= GWIPC_MESSAGE_OUTPUT_UPSERT &&
           type <= GWIPC_MESSAGE_FRAME_ACKNOWLEDGED)
    status = validate_compositor(connection, type, flags, payload, fds, codec);
  else if (type >= GWIPC_MESSAGE_SYNTHETIC_MOTION &&
           type <= GWIPC_MESSAGE_SYNTHETIC_INPUT_ACKNOWLEDGED)
    status = validate_input(type, flags, payload, codec);
  else if (type >= GWIPC_MESSAGE_SESSION_STATE_CHANGE &&
           type <= GWIPC_MESSAGE_SESSION_STATE_ACKNOWLEDGED)
    status = validate_session(type, flags, payload, codec);
  if (status != GWIPC_STATUS_OK) return status;
  if (codec != wire::CodecStatus::Ok) return GWIPC_STATUS_PROTOCOL_ERROR;
  if (type == GWIPC_MESSAGE_BUFFER_ATTACH) {
    wire::BufferAttach attachment;
    if (wire::decode(payload, attachment) != wire::CodecStatus::Ok)
      return GWIPC_STATUS_PROTOCOL_ERROR;
    const std::size_t expected =
        attachment.synchronization == wire::SynchronizationMode::EventFd
            ? 2U
            : 1U;
    if (fds.size() != expected) return GWIPC_STATUS_PROTOCOL_ERROR;
  } else if (!fds.empty()) {
    return GWIPC_STATUS_PROTOCOL_ERROR;
  }
  return validate_snapshot(
      snapshot, type, flags, payload,
      (connection.peer.capabilities & GWIPC_CAP_INTERACTIVE_POLICY) != 0 &&
          policy_output_direction(connection, direction));
}

}  // namespace gw::ipc
