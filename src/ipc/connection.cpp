#include "ipc/connection_internal.hpp"

#include "ipc/endpoint.hpp"
#include "ipc/wire/compositor_contract.hpp"
#include "ipc/wire/control.hpp"
#include "ipc/wire/envelope.hpp"
#include "ipc/wire/lifecycle_contract.hpp"
#include "ipc/wire/policy_contract.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>

namespace gw::ipc {
namespace {

using wire::MessageFlag;
using wire::MessageType;

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
    default:
      return 0;
  }
}

bool snapshot_control(std::uint16_t type) noexcept {
  return type >= GWIPC_MESSAGE_SNAPSHOT_BEGIN &&
         type <= GWIPC_MESSAGE_SNAPSHOT_ABORT;
}

bool supported_established_type(std::uint16_t type) noexcept {
  switch (type) {
    case GWIPC_MESSAGE_PING:
    case GWIPC_MESSAGE_PONG:
    case GWIPC_MESSAGE_PROTOCOL_ERROR:
    case GWIPC_MESSAGE_SNAPSHOT_BEGIN:
    case GWIPC_MESSAGE_SNAPSHOT_END:
    case GWIPC_MESSAGE_SNAPSHOT_ABORT:
    case GWIPC_MESSAGE_OUTPUT_UPSERT:
    case GWIPC_MESSAGE_OUTPUT_REMOVE:
    case GWIPC_MESSAGE_SURFACE_UPSERT:
    case GWIPC_MESSAGE_SURFACE_REMOVE:
    case GWIPC_MESSAGE_SURFACE_POLICY_UPSERT:
    case GWIPC_MESSAGE_BUFFER_ATTACH:
    case GWIPC_MESSAGE_BUFFER_DETACH:
    case GWIPC_MESSAGE_BUFFER_RELEASE:
    case GWIPC_MESSAGE_SURFACE_DAMAGE:
    case GWIPC_MESSAGE_FRAME_COMMIT:
    case GWIPC_MESSAGE_FRAME_ACKNOWLEDGED:
    case GWIPC_MESSAGE_POLICY_CONTEXT_UPSERT:
    case GWIPC_MESSAGE_POLICY_WINDOW_UPSERT:
    case GWIPC_MESSAGE_POLICY_WINDOW_REMOVE:
    case GWIPC_MESSAGE_POLICY_LIFECYCLE_WINDOW_UPSERT:
    case GWIPC_MESSAGE_POLICY_COMMIT:
    case GWIPC_MESSAGE_POLICY_WINDOW_STATE:
    case GWIPC_MESSAGE_POLICY_ACKNOWLEDGED:
      return true;
    default:
      return false;
  }
}

std::uint16_t read_u16(const std::uint8_t* bytes) noexcept {
  return static_cast<std::uint16_t>(bytes[0]) |
         (static_cast<std::uint16_t>(bytes[1]) << 8U);
}

std::uint32_t read_u32(const std::uint8_t* bytes) noexcept {
  return static_cast<std::uint32_t>(read_u16(bytes)) |
         (static_cast<std::uint32_t>(read_u16(bytes + 2)) << 16U);
}

std::uint64_t read_u64(const std::uint8_t* bytes) noexcept {
  return static_cast<std::uint64_t>(read_u32(bytes)) |
         (static_cast<std::uint64_t>(read_u32(bytes + 4)) << 32U);
}

bool parse_error_context(std::span<const std::uint8_t> record,
                         wire::Envelope& envelope) noexcept {
  if (record.size() < wire::kEnvelopeSize || record[0] != 'G' ||
      record[1] != 'W' || record[2] != 'I' || record[3] != 'P' ||
      read_u16(record.data() + 4) != wire::kEnvelopeSize ||
      read_u16(record.data() + 22) != 0)
    return false;
  envelope.major = read_u16(record.data() + 6);
  envelope.minor = read_u16(record.data() + 8);
  envelope.type = static_cast<MessageType>(read_u16(record.data() + 10));
  envelope.flags = read_u32(record.data() + 12);
  envelope.payload_size = read_u32(record.data() + 16);
  envelope.fd_count = read_u16(record.data() + 20);
  envelope.sequence = read_u64(record.data() + 24);
  envelope.reply_to = read_u64(record.data() + 32);
  return envelope.sequence != 0;
}

void set_closed(gwipc_connection& connection) noexcept {
  if (connection.state != GWIPC_CONNECTION_CLOSED)
    std::fprintf(stderr, "gwipc: disconnected connection=%llu\n",
                 static_cast<unsigned long long>(connection.peer.connection_id));
  if (connection.incoming_snapshot.active || connection.outgoing_snapshot.active)
    connection.snapshot_aborted = true;
  close_fd(connection.fd);
  connection.state = GWIPC_CONNECTION_CLOSED;
  connection.outgoing.clear();
  connection.queued_bytes = 0;
}

gwipc_status protocol_failure(gwipc_connection& connection,
                              wire::ProtocolErrorCode code,
                              const wire::Envelope& offending,
                              const char* detail,
                              gwipc_status result = GWIPC_STATUS_PROTOCOL_ERROR) {
  std::fprintf(stderr,
               "gwipc: protocol error code=%u type=%u sequence=%llu\n",
               static_cast<unsigned>(code),
               static_cast<unsigned>(offending.type),
               static_cast<unsigned long long>(offending.sequence));
  if (connection.state != GWIPC_CONNECTION_ESTABLISHED) {
    set_closed(connection);
    return result;
  }
  wire::ProtocolError error;
  error.code = code;
  error.offending_type = offending.type;
  error.offending_sequence = offending.sequence;
  error.detail = detail;
  const auto payload = wire::encode(error);
  const auto queued = queue_internal(
      connection, GWIPC_MESSAGE_PROTOCOL_ERROR,
      GWIPC_FLAG_REPLY | GWIPC_FLAG_ERROR, offending.sequence, payload);
  if (queued != GWIPC_STATUS_OK) {
    set_closed(connection);
    return result;
  }
  connection.state = GWIPC_CONNECTION_CLOSING;
  connection.close_after_flush = true;
  return result;
}

gwipc_status duplicate_fds(std::span<const int> source,
                           std::vector<int>& result) noexcept {
  try {
    result.reserve(source.size());
  } catch (...) {
    return GWIPC_STATUS_OUT_OF_MEMORY;
  }
  for (const int fd : source) {
    if (fd < 0) return GWIPC_STATUS_INVALID_ARGUMENT;
    const int duplicate = ::fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (duplicate < 0) return GWIPC_STATUS_SYSTEM_ERROR;
    result.push_back(duplicate);
  }
  return GWIPC_STATUS_OK;
}

gwipc_status validate_snapshot(SnapshotState& state, std::uint16_t type,
                               std::uint32_t flags,
                               std::span<const std::uint8_t> payload) {
  const bool item = (flags & GWIPC_FLAG_SNAPSHOT_ITEM) != 0;
  if (snapshot_control(type) && item) return GWIPC_STATUS_PROTOCOL_ERROR;
  if (type == GWIPC_MESSAGE_SNAPSHOT_BEGIN) {
    wire::SnapshotBegin begin;
    if (state.active || wire::decode(payload, begin) != wire::CodecStatus::Ok)
      return GWIPC_STATUS_PROTOCOL_ERROR;
    state = {true, begin.snapshot_id, begin.generation,
             begin.expected_item_count, 0};
    return GWIPC_STATUS_OK;
  }
  if (type == GWIPC_MESSAGE_SNAPSHOT_END) {
    wire::SnapshotEnd end;
    if (!state.active || wire::decode(payload, end) != wire::CodecStatus::Ok ||
        end.snapshot_id != state.id || end.generation != state.generation ||
        end.actual_item_count != state.item_count ||
        (state.expected_count != UINT32_MAX &&
         state.expected_count != state.item_count))
      return GWIPC_STATUS_PROTOCOL_ERROR;
    state = {};
    return GWIPC_STATUS_OK;
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

gwipc_status validate_application(gwipc_connection& connection,
                                  std::uint16_t type, std::uint32_t flags,
                                  std::span<const std::uint8_t> payload,
                                  std::span<const int> fds,
                                  SnapshotState& snapshot) {
  const auto required = required_capability(type);
  if ((required & connection.peer.capabilities) != required)
    return GWIPC_STATUS_CAPABILITY_MISMATCH;
  wire::CodecStatus codec = wire::CodecStatus::InvalidValue;
  switch (type) {
    case GWIPC_MESSAGE_PING: {
      wire::Ping value;
      codec = wire::decode(payload, value);
      if (flags != GWIPC_FLAG_ACK_REQUIRED)
        return GWIPC_STATUS_PROTOCOL_ERROR;
      break;
    }
    case GWIPC_MESSAGE_SNAPSHOT_BEGIN: {
      wire::SnapshotBegin value;
      codec = wire::decode(payload, value);
      break;
    }
    case GWIPC_MESSAGE_SNAPSHOT_END: {
      wire::SnapshotEnd value;
      codec = wire::decode(payload, value);
      break;
    }
    case GWIPC_MESSAGE_SNAPSHOT_ABORT: {
      wire::SnapshotAbort value;
      codec = wire::decode(payload, value);
      break;
    }
    case GWIPC_MESSAGE_OUTPUT_UPSERT: {
      wire::OutputUpsert value;
      codec = wire::decode(payload, value);
      break;
    }
    case GWIPC_MESSAGE_OUTPUT_REMOVE: {
      wire::OutputRemove value;
      codec = wire::decode(payload, value);
      break;
    }
    case GWIPC_MESSAGE_SURFACE_UPSERT: {
      wire::SurfaceUpsert value;
      codec = wire::decode(payload, value);
      if (codec == wire::CodecStatus::Ok && value.presentation_flags != 0 &&
          (connection.peer.capabilities & GWIPC_CAP_WINDOW_LIFECYCLE) == 0)
        return GWIPC_STATUS_CAPABILITY_MISMATCH;
      break;
    }
    case GWIPC_MESSAGE_SURFACE_REMOVE: {
      wire::SurfaceRemove value;
      codec = wire::decode(payload, value);
      break;
    }
    case GWIPC_MESSAGE_BUFFER_ATTACH: {
      wire::BufferAttach value;
      codec = wire::decode(payload, value);
      if (codec == wire::CodecStatus::Ok && fds.size() == 1) {
        struct stat status {};
        const int descriptor_flags = ::fcntl(fds[0], F_GETFL);
        if (descriptor_flags < 0 || (descriptor_flags & O_PATH) != 0 ||
            (descriptor_flags & O_ACCMODE) == O_WRONLY ||
            ::fstat(fds[0], &status) < 0 || !S_ISREG(status.st_mode) ||
            status.st_size < 0 ||
            static_cast<std::uint64_t>(status.st_size) < value.storage_size)
          return GWIPC_STATUS_PROTOCOL_ERROR;
      }
      break;
    }
    case GWIPC_MESSAGE_BUFFER_DETACH: {
      wire::BufferDetach value;
      codec = wire::decode(payload, value);
      break;
    }
    case GWIPC_MESSAGE_BUFFER_RELEASE: {
      wire::BufferRelease value;
      codec = wire::decode(payload, value);
      break;
    }
    case GWIPC_MESSAGE_SURFACE_DAMAGE: {
      wire::SurfaceDamage value;
      codec = wire::decode(payload, value);
      break;
    }
    case GWIPC_MESSAGE_FRAME_COMMIT: {
      wire::FrameCommit value;
      codec = wire::decode(payload, value);
      if (flags != GWIPC_FLAG_ACK_REQUIRED)
        return GWIPC_STATUS_PROTOCOL_ERROR;
      break;
    }
    case GWIPC_MESSAGE_FRAME_ACKNOWLEDGED: {
      wire::FrameAcknowledged value;
      codec = wire::decode(payload, value);
      if (flags != GWIPC_FLAG_REPLY) return GWIPC_STATUS_PROTOCOL_ERROR;
      break;
    }
    case GWIPC_MESSAGE_POLICY_CONTEXT_UPSERT: {
      wire::PolicyContextUpsert value;
      codec = wire::decode(payload, value);
      if (flags != 0 && flags != GWIPC_FLAG_SNAPSHOT_ITEM)
        return GWIPC_STATUS_PROTOCOL_ERROR;
      break;
    }
    case GWIPC_MESSAGE_POLICY_WINDOW_UPSERT: {
      wire::PolicyWindowUpsert value;
      codec = wire::decode(payload, value);
      if (flags != 0 && flags != GWIPC_FLAG_SNAPSHOT_ITEM)
        return GWIPC_STATUS_PROTOCOL_ERROR;
      break;
    }
    case GWIPC_MESSAGE_POLICY_WINDOW_REMOVE: {
      wire::PolicyWindowRemove value;
      codec = wire::decode(payload, value);
      if (flags != 0)
        return GWIPC_STATUS_PROTOCOL_ERROR;
      break;
    }
    case GWIPC_MESSAGE_POLICY_COMMIT: {
      wire::PolicyCommit value;
      codec = wire::decode(payload, value);
      if (flags != GWIPC_FLAG_ACK_REQUIRED || snapshot.active)
        return GWIPC_STATUS_PROTOCOL_ERROR;
      break;
    }
    case GWIPC_MESSAGE_POLICY_WINDOW_STATE: {
      wire::PolicyWindowState value;
      codec = wire::decode(payload, value);
      if (flags != GWIPC_FLAG_SNAPSHOT_ITEM)
        return GWIPC_STATUS_PROTOCOL_ERROR;
      break;
    }
    case GWIPC_MESSAGE_POLICY_ACKNOWLEDGED: {
      wire::PolicyAcknowledged value;
      codec = wire::decode(payload, value);
      if (flags != GWIPC_FLAG_REPLY)
        return GWIPC_STATUS_PROTOCOL_ERROR;
      break;
    }
    case GWIPC_MESSAGE_POLICY_LIFECYCLE_WINDOW_UPSERT: {
      wire::PolicyLifecycleWindowUpsert value;
      codec = wire::decode(payload, value);
      if (flags != 0 && flags != GWIPC_FLAG_SNAPSHOT_ITEM)
        return GWIPC_STATUS_PROTOCOL_ERROR;
      break;
    }
    case GWIPC_MESSAGE_SURFACE_POLICY_UPSERT: {
      wire::SurfacePolicyUpsert value;
      codec = wire::decode(payload, value);
      if (flags != GWIPC_FLAG_SNAPSHOT_ITEM)
        return GWIPC_STATUS_PROTOCOL_ERROR;
      break;
    }
    case GWIPC_MESSAGE_PONG: {
      wire::Pong value;
      codec = wire::decode(payload, value);
      if (flags != GWIPC_FLAG_REPLY) return GWIPC_STATUS_PROTOCOL_ERROR;
      break;
    }
    case GWIPC_MESSAGE_PROTOCOL_ERROR: {
      wire::ProtocolError value;
      codec = wire::decode(payload, value);
      if (flags != (GWIPC_FLAG_REPLY | GWIPC_FLAG_ERROR))
        return GWIPC_STATUS_PROTOCOL_ERROR;
      break;
    }
    default:
      return GWIPC_STATUS_PROTOCOL_ERROR;
  }
  if (codec != wire::CodecStatus::Ok) return GWIPC_STATUS_PROTOCOL_ERROR;
  if (type == GWIPC_MESSAGE_BUFFER_ATTACH) {
    if (fds.size() != 1) return GWIPC_STATUS_PROTOCOL_ERROR;
  } else if (!fds.empty()) {
    return GWIPC_STATUS_PROTOCOL_ERROR;
  }
  return validate_snapshot(snapshot, type, flags, payload);
}

gwipc_status queue_hello(gwipc_connection& connection) {
  wire::Hello hello;
  hello.sender_role = static_cast<wire::Role>(connection.config.local_role);
  hello.offered_capabilities = connection.config.offered_capabilities;
  hello.required_capabilities = connection.config.required_peer_capabilities;
  hello.maximum_payload = connection.config.maximum_payload;
  hello.maximum_fd_count = connection.config.maximum_fd_count;
  hello.sender_instance_id = connection.config.instance_id;
  hello.name = connection.config.label;
  const auto payload = wire::encode(hello);
  return queue_internal(connection, GWIPC_MESSAGE_HELLO, 0, 0, payload);
}

gwipc_status reject(gwipc_connection& connection, wire::RejectReason reason,
                    std::string detail, gwipc_status result) {
  std::fprintf(stderr, "gwipc: handshake rejected reason=%u\n",
               static_cast<unsigned>(reason));
  wire::Reject reject_message;
  reject_message.reason = reason;
  reject_message.detail = std::move(detail);
  const auto payload = wire::encode(reject_message);
  if (queue_internal(connection, GWIPC_MESSAGE_REJECT, GWIPC_FLAG_REPLY, 1,
                     payload) != GWIPC_STATUS_OK) {
    set_closed(connection);
    return result;
  }
  connection.state = GWIPC_CONNECTION_REJECTING;
  connection.close_after_flush = true;
  return result;
}

gwipc_status handle_hello(gwipc_connection& connection,
                          const wire::Envelope& envelope,
                          std::span<const std::uint8_t> payload) {
  wire::Hello hello;
  if (envelope.type != MessageType::Hello || envelope.flags != 0 ||
      envelope.fd_count != 0 ||
      wire::decode(payload, hello) != wire::CodecStatus::Ok) {
    return reject(connection, wire::RejectReason::InvalidHello, "invalid hello",
                  GWIPC_STATUS_PROTOCOL_ERROR);
  }
  if (hello.minimum_major > wire::kWireMajor ||
      hello.maximum_major < wire::kWireMajor || hello.minimum_minor > 0 ||
      hello.maximum_minor < wire::kWireMinor) {
    return reject(connection, wire::RejectReason::IncompatibleVersion,
                  "wire version mismatch", GWIPC_STATUS_VERSION_MISMATCH);
  }
  const auto role = static_cast<std::uint16_t>(hello.sender_role);
  if (role == 0 || role > GWIPC_ROLE_DIAGNOSTIC_TOOL ||
      (connection.config.peer_roles & (UINT64_C(1) << role)) == 0) {
    return reject(connection, wire::RejectReason::RoleNotAllowed,
                  "peer role is not allowed", GWIPC_STATUS_ROLE_REJECTED);
  }
  if (hello.maximum_payload == 0 ||
      hello.maximum_payload > GWIPC_HARD_MAXIMUM_PAYLOAD ||
      hello.maximum_fd_count > GWIPC_HARD_MAXIMUM_FDS ||
      (hello.required_capabilities & ~kKnownCapabilities) != 0) {
    return reject(connection, wire::RejectReason::InvalidHello,
                  "invalid offered limits", GWIPC_STATUS_PROTOCOL_ERROR);
  }
  const auto negotiated = hello.offered_capabilities &
                          connection.config.offered_capabilities &
                          kKnownCapabilities;
  if ((hello.required_capabilities & negotiated) !=
          hello.required_capabilities ||
      (connection.config.required_peer_capabilities & negotiated) !=
          connection.config.required_peer_capabilities) {
    return reject(connection, wire::RejectReason::CapabilityMismatch,
                  "required capability is unavailable",
                  GWIPC_STATUS_CAPABILITY_MISMATCH);
  }
  connection.peer.role = static_cast<gwipc_role>(role);
  connection.peer.wire_version = {wire::kWireMajor, wire::kWireMinor};
  connection.peer.capabilities = negotiated;
  connection.peer.maximum_payload =
      std::min(connection.config.maximum_payload, hello.maximum_payload);
  connection.peer.maximum_fd_count =
      std::min(connection.config.maximum_fd_count, hello.maximum_fd_count);
  connection.peer.connection_id = connection.assigned_connection_id;

  wire::Welcome welcome;
  welcome.sender_role = static_cast<wire::Role>(connection.config.local_role);
  welcome.negotiated_capabilities = negotiated;
  welcome.negotiated_maximum_payload = connection.peer.maximum_payload;
  welcome.negotiated_maximum_fd_count = connection.peer.maximum_fd_count;
  welcome.connection_id = connection.assigned_connection_id;
  welcome.sender_instance_id = connection.config.instance_id;
  const auto welcome_payload = wire::encode(welcome);
  const auto status = queue_internal(connection, GWIPC_MESSAGE_WELCOME,
                                     GWIPC_FLAG_REPLY, envelope.sequence,
                                     welcome_payload);
  if (status == GWIPC_STATUS_OK)
    connection.state = GWIPC_CONNECTION_ESTABLISHED;
  if (status == GWIPC_STATUS_OK)
    std::fprintf(stderr,
                 "gwipc: handshake accepted connection=%llu role=%u wire=1.0\n",
                 static_cast<unsigned long long>(
                     connection.peer.connection_id),
                 static_cast<unsigned>(connection.peer.role));
  return status;
}

gwipc_status handle_welcome(gwipc_connection& connection,
                            const wire::Envelope& envelope,
                            std::span<const std::uint8_t> payload) {
  if (envelope.type == MessageType::Reject) {
    wire::Reject rejection;
    if (envelope.flags != GWIPC_FLAG_REPLY || envelope.reply_to != 1 ||
        envelope.fd_count != 0 ||
        wire::decode(payload, rejection) != wire::CodecStatus::Ok) {
      set_closed(connection);
      return GWIPC_STATUS_PROTOCOL_ERROR;
    }
    set_closed(connection);
    switch (rejection.reason) {
      case wire::RejectReason::IncompatibleVersion:
        return GWIPC_STATUS_VERSION_MISMATCH;
      case wire::RejectReason::RoleNotAllowed:
        return GWIPC_STATUS_ROLE_REJECTED;
      case wire::RejectReason::CapabilityMismatch:
        return GWIPC_STATUS_CAPABILITY_MISMATCH;
      case wire::RejectReason::CredentialRejected:
        return GWIPC_STATUS_CREDENTIAL_REJECTED;
      default:
        return GWIPC_STATUS_PROTOCOL_ERROR;
    }
  }
  wire::Welcome welcome;
  if (envelope.type != MessageType::Welcome ||
      envelope.flags != GWIPC_FLAG_REPLY || envelope.reply_to != 1 ||
      envelope.fd_count != 0 ||
      wire::decode(payload, welcome) != wire::CodecStatus::Ok) {
    set_closed(connection);
    return GWIPC_STATUS_PROTOCOL_ERROR;
  }
  const auto role = static_cast<std::uint16_t>(welcome.sender_role);
  if (role == 0 || role > GWIPC_ROLE_DIAGNOSTIC_TOOL ||
      (connection.config.peer_roles & (UINT64_C(1) << role)) == 0) {
    set_closed(connection);
    return GWIPC_STATUS_ROLE_REJECTED;
  }
  if (welcome.selected_major != wire::kWireMajor ||
      welcome.selected_minor != wire::kWireMinor) {
    set_closed(connection);
    return GWIPC_STATUS_VERSION_MISMATCH;
  }
  if ((welcome.negotiated_capabilities &
       ~connection.config.offered_capabilities) != 0 ||
      (connection.config.required_peer_capabilities &
       welcome.negotiated_capabilities) !=
          connection.config.required_peer_capabilities ||
      welcome.negotiated_maximum_payload == 0 ||
      welcome.negotiated_maximum_payload > connection.config.maximum_payload ||
      welcome.negotiated_maximum_fd_count >
          connection.config.maximum_fd_count ||
      welcome.connection_id == 0) {
    set_closed(connection);
    return GWIPC_STATUS_PROTOCOL_ERROR;
  }
  connection.peer.role = static_cast<gwipc_role>(role);
  connection.peer.wire_version = {welcome.selected_major,
                                  welcome.selected_minor};
  connection.peer.capabilities = welcome.negotiated_capabilities;
  connection.peer.maximum_payload = welcome.negotiated_maximum_payload;
  connection.peer.maximum_fd_count = welcome.negotiated_maximum_fd_count;
  connection.peer.connection_id = welcome.connection_id;
  connection.pending_replies.erase(1);
  connection.state = GWIPC_CONNECTION_ESTABLISHED;
  std::fprintf(stderr,
               "gwipc: handshake accepted connection=%llu role=%u wire=1.0\n",
               static_cast<unsigned long long>(connection.peer.connection_id),
               static_cast<unsigned>(connection.peer.role));
  return GWIPC_STATUS_OK;
}

gwipc_status flush(gwipc_connection& connection) noexcept {
  while (!connection.outgoing.empty()) {
    auto& record = connection.outgoing.front();
    iovec vector{record.bytes.data(), record.bytes.size()};
    std::array<std::byte, CMSG_SPACE(sizeof(int) * GWIPC_HARD_MAXIMUM_FDS)>
        control{};
    msghdr message{};
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    if (!record.fds.empty()) {
      message.msg_control = control.data();
      message.msg_controllen = CMSG_SPACE(sizeof(int) * record.fds.size());
      auto* header = CMSG_FIRSTHDR(&message);
      header->cmsg_level = SOL_SOCKET;
      header->cmsg_type = SCM_RIGHTS;
      header->cmsg_len = CMSG_LEN(sizeof(int) * record.fds.size());
      std::memcpy(CMSG_DATA(header), record.fds.data(),
                  sizeof(int) * record.fds.size());
    }
    const auto sent = ::sendmsg(connection.fd, &message, MSG_NOSIGNAL);
    if (sent < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return GWIPC_STATUS_WOULD_BLOCK;
      connection.system_errno = errno;
      set_closed(connection);
      return GWIPC_STATUS_SYSTEM_ERROR;
    }
    if (static_cast<std::size_t>(sent) != record.bytes.size()) {
      set_closed(connection);
      return GWIPC_STATUS_SYSTEM_ERROR;
    }
    connection.queued_bytes -= record.bytes.size();
    connection.outgoing.pop_front();
  }
  if (connection.close_after_flush) set_closed(connection);
  return GWIPC_STATUS_OK;
}

gwipc_status receive_one(gwipc_connection& connection) {
  const auto maximum_payload = connection.state == GWIPC_CONNECTION_ESTABLISHED
                                   ? connection.peer.maximum_payload
                                   : connection.config.maximum_payload;
  std::vector<std::uint8_t> bytes;
  try {
    bytes.resize(wire::kEnvelopeSize + maximum_payload);
  } catch (...) {
    return GWIPC_STATUS_OUT_OF_MEMORY;
  }
  std::array<std::byte, CMSG_SPACE(sizeof(int) * GWIPC_HARD_MAXIMUM_FDS)>
      control{};
  std::vector<int> fds;
  fds.reserve(GWIPC_HARD_MAXIMUM_FDS);
  iovec vector{bytes.data(), bytes.size()};
  msghdr message{};
  message.msg_iov = &vector;
  message.msg_iovlen = 1;
  message.msg_control = control.data();
  message.msg_controllen = control.size();
  const auto received = ::recvmsg(connection.fd, &message,
                                  MSG_DONTWAIT | MSG_CMSG_CLOEXEC | MSG_TRUNC);
  if (received < 0) {
    if (errno == EINTR) return GWIPC_STATUS_OK;
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return GWIPC_STATUS_WOULD_BLOCK;
    connection.system_errno = errno;
    set_closed(connection);
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
  if (received == 0) {
    set_closed(connection);
    return GWIPC_STATUS_DISCONNECTED;
  }

  const auto maximum_fds = connection.state == GWIPC_CONNECTION_ESTABLISHED
                               ? connection.peer.maximum_fd_count
                               : connection.config.maximum_fd_count;
  bool ancillary_invalid = (message.msg_flags & MSG_CTRUNC) != 0;
  for (auto* header = CMSG_FIRSTHDR(&message); header;
       header = CMSG_NXTHDR(&message, header)) {
    if (header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS ||
        header->cmsg_len < CMSG_LEN(0)) {
      ancillary_invalid = true;
      continue;
    }
    const auto descriptor_bytes = header->cmsg_len - CMSG_LEN(0);
    if (descriptor_bytes % sizeof(int) != 0) {
      ancillary_invalid = true;
      continue;
    }
    const auto count = descriptor_bytes / sizeof(int);
    const auto* descriptors = reinterpret_cast<const int*>(CMSG_DATA(header));
    for (std::size_t index = 0; index < count; ++index) {
      if (fds.size() < GWIPC_HARD_MAXIMUM_FDS) {
        fds.push_back(descriptors[index]);
      } else {
        int excess = descriptors[index];
        close_fd(excess);
        ancillary_invalid = true;
      }
    }
  }
  auto close_received = [&] {
    for (auto& fd : fds) close_fd(fd);
  };
  const auto record_size = std::min<std::size_t>(
      received > 0 ? static_cast<std::size_t>(received) : 0, bytes.size());
  wire::Envelope error_context;
  const bool has_error_context =
      parse_error_context(std::span(bytes).first(record_size), error_context);
  if (ancillary_invalid || (message.msg_flags & MSG_TRUNC) != 0 ||
      static_cast<std::size_t>(received) > bytes.size() ||
      fds.size() > maximum_fds) {
    close_received();
    if (has_error_context) {
      const auto code = ancillary_invalid || fds.size() > maximum_fds
                            ? wire::ProtocolErrorCode::InvalidDescriptorCount
                            : wire::ProtocolErrorCode::LimitExceeded;
      return protocol_failure(connection, code, error_context,
                              "record or descriptor limit exceeded");
    }
    set_closed(connection);
    return GWIPC_STATUS_PROTOCOL_ERROR;
  }
  bytes.resize(static_cast<std::size_t>(received));
  wire::Envelope envelope;
  const auto envelope_status =
      wire::decode_envelope(bytes, fds.size(), maximum_payload, envelope);
  if (envelope_status != wire::CodecStatus::Ok) {
    close_received();
    if (has_error_context) {
      const auto code = error_context.fd_count != fds.size()
                            ? wire::ProtocolErrorCode::InvalidDescriptorCount
                        : envelope_status == wire::CodecStatus::LimitExceeded
                            ? wire::ProtocolErrorCode::LimitExceeded
                            : wire::ProtocolErrorCode::MalformedEnvelope;
      return protocol_failure(connection, code, error_context,
                              "malformed message envelope");
    }
    set_closed(connection);
    return GWIPC_STATUS_PROTOCOL_ERROR;
  }
  if (envelope.sequence != connection.next_receive_sequence) {
    close_received();
    return protocol_failure(connection,
                            wire::ProtocolErrorCode::OutOfOrderSequence,
                            envelope, "out-of-order sequence");
  }
#ifdef GWIPC_TRACE
  std::fprintf(stderr,
               "gwipc: receive role=%u capabilities=0x%llx type=%u sequence=%llu reply_to=%llu payload=%u fds=%u\n",
               static_cast<unsigned>(connection.peer.role),
               static_cast<unsigned long long>(connection.peer.capabilities),
               static_cast<unsigned>(envelope.type),
               static_cast<unsigned long long>(envelope.sequence),
               static_cast<unsigned long long>(envelope.reply_to),
               envelope.payload_size, envelope.fd_count);
#endif
  if (connection.next_receive_sequence == UINT64_MAX) {
    close_received();
    set_closed(connection);
    return GWIPC_STATUS_LIMIT_EXCEEDED;
  }
  ++connection.next_receive_sequence;
  const auto payload = std::span(bytes).subspan(wire::kEnvelopeSize);
  if (connection.state == GWIPC_CONNECTION_AWAITING_HELLO) {
    close_received();
    return handle_hello(connection, envelope, payload);
  }
  if (connection.state == GWIPC_CONNECTION_AWAITING_WELCOME) {
    close_received();
    return handle_welcome(connection, envelope, payload);
  }
  if (connection.state != GWIPC_CONNECTION_ESTABLISHED) {
    close_received();
    return GWIPC_STATUS_INVALID_STATE;
  }
  if (envelope.major != connection.peer.wire_version.major ||
      envelope.minor != connection.peer.wire_version.minor ||
      envelope.type == MessageType::Hello ||
      envelope.type == MessageType::Welcome ||
      envelope.type == MessageType::Reject) {
    close_received();
    return protocol_failure(connection,
                            wire::ProtocolErrorCode::MalformedEnvelope,
                            envelope, "invalid established message envelope");
  }
  if (wire::has_flag(envelope.flags, MessageFlag::Reply)) {
    if (envelope.type == MessageType::Pong) {
      wire::Pong pong;
      const auto expected = connection.pending_ping_nonces.find(envelope.reply_to);
      if (expected == connection.pending_ping_nonces.end() ||
          wire::decode(payload, pong) != wire::CodecStatus::Ok ||
          pong.nonce != expected->second) {
        close_received();
        return protocol_failure(connection,
                                wire::ProtocolErrorCode::UnexpectedReply,
                                envelope, "unexpected pong reply");
      }
      connection.pending_ping_nonces.erase(expected);
    }
    if (envelope.type == MessageType::FrameAcknowledged) {
      wire::FrameAcknowledged acknowledged;
      const auto expected =
          connection.pending_frame_commits.find(envelope.reply_to);
      if (expected == connection.pending_frame_commits.end() ||
          wire::decode(payload, acknowledged) != wire::CodecStatus::Ok ||
          acknowledged.commit_id != expected->second) {
        close_received();
        return protocol_failure(connection,
                                wire::ProtocolErrorCode::UnexpectedReply,
                                envelope,
                                "frame acknowledgement does not match commit");
      }
      connection.pending_frame_commits.erase(expected);
    }
    if (envelope.type == MessageType::PolicyAcknowledged) {
      wire::PolicyAcknowledged acknowledged;
      const auto expected =
          connection.pending_policy_commits.find(envelope.reply_to);
      if (expected == connection.pending_policy_commits.end() ||
          wire::decode(payload, acknowledged) != wire::CodecStatus::Ok ||
          acknowledged.commit_id != expected->second) {
        close_received();
        return protocol_failure(
            connection, wire::ProtocolErrorCode::UnexpectedReply, envelope,
            "policy acknowledgement does not match commit");
      }
      connection.pending_policy_commits.erase(expected);
    }
    const bool protocol_error = envelope.type == MessageType::ProtocolError;
    if (connection.pending_replies.erase(envelope.reply_to) == 0 &&
        !(protocol_error && envelope.reply_to > 0 &&
          envelope.reply_to < connection.next_send_sequence)) {
      close_received();
      return protocol_failure(connection,
                              wire::ProtocolErrorCode::UnexpectedReply,
                              envelope, "unexpected reply correlation");
    }
  }
  if (envelope.type == MessageType::Ping) {
    wire::Ping ping;
    if (envelope.flags != GWIPC_FLAG_ACK_REQUIRED || envelope.reply_to != 0 ||
        !fds.empty() || wire::decode(payload, ping) != wire::CodecStatus::Ok) {
      close_received();
      return protocol_failure(
          connection,
          !fds.empty() ? wire::ProtocolErrorCode::InvalidDescriptorCount
                       : wire::ProtocolErrorCode::MalformedPayload,
          envelope, "invalid ping message");
    }
    close_received();
    const auto pong = wire::encode(wire::Pong{ping.nonce});
    return queue_internal(connection, GWIPC_MESSAGE_PONG, GWIPC_FLAG_REPLY,
                          envelope.sequence, pong);
  }
  if (!supported_established_type(static_cast<std::uint16_t>(envelope.type))) {
    std::fprintf(stderr,
                 "gwipc: protocol error code=UnsupportedMessage type=%u sequence=%llu\n",
                 static_cast<unsigned>(envelope.type),
                 static_cast<unsigned long long>(envelope.sequence));
    close_received();
    wire::ProtocolError error;
    error.code = wire::ProtocolErrorCode::UnsupportedMessage;
    error.offending_type = envelope.type;
    error.offending_sequence = envelope.sequence;
    error.detail = "unsupported message type";
    const auto error_payload = wire::encode(error);
    const auto queued = queue_internal(
        connection, GWIPC_MESSAGE_PROTOCOL_ERROR,
        GWIPC_FLAG_REPLY | GWIPC_FLAG_ERROR, envelope.sequence, error_payload);
    if (queued != GWIPC_STATUS_OK) {
      set_closed(connection);
      return queued;
    }
    if (wire::has_flag(envelope.flags, MessageFlag::Critical)) {
      connection.state = GWIPC_CONNECTION_CLOSING;
      connection.close_after_flush = true;
      return GWIPC_STATUS_PROTOCOL_ERROR;
    }
    return GWIPC_STATUS_OK;
  }
  const auto application_status = validate_application(
      connection, static_cast<std::uint16_t>(envelope.type), envelope.flags,
      payload, fds, connection.incoming_snapshot);
  if (application_status != GWIPC_STATUS_OK) {
    close_received();
    wire::ProtocolErrorCode code = wire::ProtocolErrorCode::MalformedPayload;
    if (application_status == GWIPC_STATUS_CAPABILITY_MISMATCH) {
      code = wire::ProtocolErrorCode::MissingCapability;
    } else if (snapshot_control(static_cast<std::uint16_t>(envelope.type)) ||
               wire::has_flag(envelope.flags, MessageFlag::SnapshotItem)) {
      code = wire::ProtocolErrorCode::SnapshotViolation;
    } else if (envelope.type == MessageType::BufferAttach) {
      code = fds.size() == 1 ? wire::ProtocolErrorCode::InvalidDescriptor
                             : wire::ProtocolErrorCode::InvalidDescriptorCount;
    }
    return protocol_failure(connection, code, envelope,
                            "invalid application message", application_status);
  }
  bool tracked_frame_commit = false;
  if (envelope.type == MessageType::FrameCommit) {
    wire::FrameCommit commit;
    if (wire::decode(payload, commit) != wire::CodecStatus::Ok ||
        connection.incoming_frame_commits.size() >=
            connection.config.maximum_queued_messages) {
      close_received();
      return protocol_failure(connection, wire::ProtocolErrorCode::LimitExceeded,
                              envelope,
                              "frame acknowledgement tracking limit exceeded",
                              GWIPC_STATUS_LIMIT_EXCEEDED);
    }
    connection.incoming_frame_commits.emplace(envelope.sequence,
                                               commit.commit_id);
    tracked_frame_commit = true;
  }
  bool tracked_policy_commit = false;
  if (envelope.type == MessageType::PolicyCommit) {
    wire::PolicyCommit commit;
    if (wire::decode(payload, commit) != wire::CodecStatus::Ok ||
        connection.incoming_policy_commits.size() >=
            connection.config.maximum_queued_messages) {
      close_received();
      return protocol_failure(connection,
                              wire::ProtocolErrorCode::LimitExceeded, envelope,
                              "policy acknowledgement tracking limit exceeded",
                              GWIPC_STATUS_LIMIT_EXCEEDED);
    }
    connection.incoming_policy_commits.emplace(envelope.sequence,
                                               commit.commit_id);
    tracked_policy_commit = true;
  }
  if (connection.incoming.size() >= connection.config.maximum_queued_messages) {
    if (tracked_frame_commit)
      connection.incoming_frame_commits.erase(envelope.sequence);
    if (tracked_policy_commit)
      connection.incoming_policy_commits.erase(envelope.sequence);
    close_received();
    return protocol_failure(connection, wire::ProtocolErrorCode::LimitExceeded,
                            envelope, "incoming queue limit exceeded",
                            GWIPC_STATUS_LIMIT_EXCEEDED);
  }
  auto received_message = std::unique_ptr<gwipc_message>(
      new (std::nothrow) gwipc_message);
  if (!received_message) {
    if (tracked_frame_commit)
      connection.incoming_frame_commits.erase(envelope.sequence);
    if (tracked_policy_commit)
      connection.incoming_policy_commits.erase(envelope.sequence);
    close_received();
    return GWIPC_STATUS_OUT_OF_MEMORY;
  }
  received_message->type = static_cast<std::uint16_t>(envelope.type);
  received_message->flags = envelope.flags;
  received_message->sequence = envelope.sequence;
  received_message->reply_to = envelope.reply_to;
  try {
    received_message->payload.assign(payload.begin(), payload.end());
    received_message->fds = std::move(fds);
    connection.incoming.push_back(received_message.release());
  } catch (...) {
    if (tracked_frame_commit)
      connection.incoming_frame_commits.erase(envelope.sequence);
    close_received();
    return GWIPC_STATUS_OUT_OF_MEMORY;
  }
  return GWIPC_STATUS_OK;
}

}  // namespace

gwipc_status queue_internal(gwipc_connection& connection, std::uint16_t type,
                            std::uint32_t flags, std::uint64_t reply_to,
                            std::span<const std::uint8_t> payload,
                            std::span<const int> fds) {
  if (connection.next_send_sequence == 0 ||
      connection.next_send_sequence == UINT64_MAX) {
    set_closed(connection);
    return GWIPC_STATUS_LIMIT_EXCEEDED;
  }
  const auto maximum_payload = connection.state == GWIPC_CONNECTION_ESTABLISHED
                                   ? connection.peer.maximum_payload
                                   : connection.config.maximum_payload;
  const auto maximum_fds = connection.state == GWIPC_CONNECTION_ESTABLISHED
                               ? connection.peer.maximum_fd_count
                               : connection.config.maximum_fd_count;
  if (payload.size() > maximum_payload || fds.size() > maximum_fds)
    return GWIPC_STATUS_LIMIT_EXCEEDED;
  if (connection.outgoing.size() >= connection.config.maximum_queued_messages ||
      payload.size() + wire::kEnvelopeSize >
          connection.config.maximum_queued_bytes -
              std::min<std::size_t>(connection.queued_bytes,
                                    connection.config.maximum_queued_bytes)) {
    set_closed(connection);
    return GWIPC_STATUS_LIMIT_EXCEEDED;
  }
  QueuedRecord record;
  auto status = duplicate_fds(fds, record.fds);
  if (status != GWIPC_STATUS_OK) {
    if (status == GWIPC_STATUS_SYSTEM_ERROR) connection.system_errno = errno;
    return status;
  }
  wire::Envelope envelope;
  envelope.type = static_cast<MessageType>(type);
  envelope.flags = flags;
  envelope.payload_size = static_cast<std::uint32_t>(payload.size());
  envelope.fd_count = static_cast<std::uint16_t>(fds.size());
  envelope.sequence = connection.next_send_sequence;
  envelope.reply_to = reply_to;
#ifdef GWIPC_TRACE
  std::fprintf(stderr,
               "gwipc: send role=%u capabilities=0x%llx type=%u sequence=%llu reply_to=%llu payload=%zu fds=%zu\n",
               static_cast<unsigned>(connection.peer.role),
               static_cast<unsigned long long>(connection.peer.capabilities),
               static_cast<unsigned>(type),
               static_cast<unsigned long long>(envelope.sequence),
               static_cast<unsigned long long>(reply_to), payload.size(),
               fds.size());
#endif
  const auto header = wire::encode_envelope(envelope);
  try {
    record.bytes.reserve(header.size() + payload.size());
    record.bytes.insert(record.bytes.end(), header.begin(), header.end());
    record.bytes.insert(record.bytes.end(), payload.begin(), payload.end());
  } catch (...) {
    return GWIPC_STATUS_OUT_OF_MEMORY;
  }
  record.sequence = connection.next_send_sequence;
  const bool expects_reply = (flags & GWIPC_FLAG_ACK_REQUIRED) != 0;
  if (expects_reply &&
      connection.pending_replies.size() >=
          connection.config.maximum_queued_messages) {
    set_closed(connection);
    return GWIPC_STATUS_LIMIT_EXCEEDED;
  }
  if (expects_reply) connection.pending_replies.insert(record.sequence);
  const auto record_size = record.bytes.size();
  try {
    connection.outgoing.push_back(std::move(record));
  } catch (...) {
    if (expects_reply) connection.pending_replies.erase(connection.next_send_sequence);
    throw;
  }
  ++connection.next_send_sequence;
  connection.queued_bytes += record_size;
  return GWIPC_STATUS_OK;
}

}  // namespace gw::ipc

extern "C" {

short gwipc_connection_wanted_poll_events(const gwipc_connection* connection) {
  if (!connection || connection->state == GWIPC_CONNECTION_CLOSED) return 0;
  short events = POLLIN;
  if (connection->state == GWIPC_CONNECTION_CONNECTING ||
      (!connection->server_side &&
       connection->state == GWIPC_CONNECTION_AWAITING_WELCOME &&
       connection->next_send_sequence == 1) ||
      !connection->outgoing.empty())
    events |= POLLOUT;
  return events;
}

gwipc_status gwipc_connection_process_poll_events(gwipc_connection* connection,
                                                  short revents) {
  try {
  if (!connection) return GWIPC_STATUS_INVALID_ARGUMENT;
  if (connection->state == GWIPC_CONNECTION_CLOSED)
    return GWIPC_STATUS_DISCONNECTED;
  if (connection->state == GWIPC_CONNECTION_CONNECTING &&
      (revents & POLLOUT) != 0) {
    int error = 0;
    socklen_t size = sizeof(error);
    if (::getsockopt(connection->fd, SOL_SOCKET, SO_ERROR, &error, &size) < 0 ||
        error != 0) {
      connection->system_errno = error != 0 ? error : errno;
      gw::ipc::set_closed(*connection);
      return GWIPC_STATUS_SYSTEM_ERROR;
    }
    connection->state = GWIPC_CONNECTION_AWAITING_WELCOME;
    if (!gw::ipc::read_peer_credentials(connection->fd, connection->peer,
                                        connection->system_errno)) {
      gw::ipc::set_closed(*connection);
      return GWIPC_STATUS_SYSTEM_ERROR;
    }
    connection->peer_credentials_read = true;
    if (connection->config.require_same_uid &&
        connection->peer.uid != static_cast<std::uint32_t>(::geteuid())) {
      gw::ipc::set_closed(*connection);
      return GWIPC_STATUS_CREDENTIAL_REJECTED;
    }
  }
  if (!connection->server_side &&
      connection->state == GWIPC_CONNECTION_AWAITING_WELCOME &&
      connection->next_send_sequence == 1) {
    const auto status = gw::ipc::queue_hello(*connection);
    if (status != GWIPC_STATUS_OK) return status;
  }
  gwipc_status result = GWIPC_STATUS_OK;
  if ((revents & POLLIN) != 0) {
    for (;;) {
      const auto status = gw::ipc::receive_one(*connection);
      if (status == GWIPC_STATUS_WOULD_BLOCK) break;
      if (status != GWIPC_STATUS_OK) {
        result = status;
        if (status == GWIPC_STATUS_PROTOCOL_ERROR ||
            status == GWIPC_STATUS_LIMIT_EXCEEDED ||
            status == GWIPC_STATUS_CAPABILITY_MISMATCH)
          std::fprintf(stderr,
                       "gwipc: protocol failure status=%s connection=%llu\n",
                       gwipc_status_string(status),
                       static_cast<unsigned long long>(
                           connection->peer.connection_id));
      }
      if (connection->state == GWIPC_CONNECTION_CLOSED ||
          status == GWIPC_STATUS_DISCONNECTED ||
          status == GWIPC_STATUS_SYSTEM_ERROR ||
          status == GWIPC_STATUS_PROTOCOL_ERROR)
        break;
    }
  }
  if ((revents & (POLLERR | POLLNVAL)) != 0) {
    gw::ipc::set_closed(*connection);
    return GWIPC_STATUS_DISCONNECTED;
  }
  if (!connection->outgoing.empty() &&
      ((revents & POLLOUT) != 0 ||
       connection->state == GWIPC_CONNECTION_REJECTING)) {
    const auto status = gw::ipc::flush(*connection);
    if (status != GWIPC_STATUS_OK && status != GWIPC_STATUS_WOULD_BLOCK)
      return status;
  }
  if ((revents & POLLHUP) != 0 && connection->outgoing.empty()) {
    gw::ipc::set_closed(*connection);
    return result == GWIPC_STATUS_OK ? GWIPC_STATUS_DISCONNECTED : result;
  }
  return result;
  } catch (const std::bad_alloc&) {
    return GWIPC_STATUS_OUT_OF_MEMORY;
  } catch (...) {
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
}

gwipc_status gwipc_connection_enqueue(gwipc_connection* connection,
                                      const gwipc_outgoing_message* message) {
  try {
  if (!connection || !message ||
      message->struct_size < sizeof(*message) ||
      std::any_of(std::begin(message->reserved), std::end(message->reserved),
                  [](auto value) { return value != 0; }) ||
      (message->payload_size != 0 && !message->payload) ||
      (message->fd_count != 0 && !message->fds))
    return GWIPC_STATUS_INVALID_ARGUMENT;
  if (connection->state != GWIPC_CONNECTION_ESTABLISHED)
    return GWIPC_STATUS_INVALID_STATE;
  if ((message->flags & ~gw::ipc::wire::kKnownMessageFlags) != 0 ||
      ((message->flags & GWIPC_FLAG_ERROR) != 0 &&
       (message->flags & GWIPC_FLAG_REPLY) == 0) ||
      ((message->flags & GWIPC_FLAG_REPLY) != 0) !=
          (message->reply_to != 0))
    return GWIPC_STATUS_INVALID_ARGUMENT;
  const auto payload = std::span(message->payload, message->payload_size);
  const auto fds = std::span(message->fds, message->fd_count);
  auto snapshot = connection->outgoing_snapshot;
  const auto validation = gw::ipc::validate_application(
      *connection, message->type, message->flags, payload, fds,
      snapshot);
  if (validation != GWIPC_STATUS_OK) return validation;
  bool ping_inserted = false;
  bool frame_commit_inserted = false;
  bool frame_acknowledges_incoming = false;
  bool policy_commit_inserted = false;
  bool policy_acknowledges_incoming = false;
  const auto pending_sequence = connection->next_send_sequence;
  if (message->type == GWIPC_MESSAGE_PING) {
    gw::ipc::wire::Ping ping;
    if (gw::ipc::wire::decode(payload, ping) !=
        gw::ipc::wire::CodecStatus::Ok)
      return GWIPC_STATUS_PROTOCOL_ERROR;
    connection->pending_ping_nonces.emplace(pending_sequence, ping.nonce);
    ping_inserted = true;
  }
  if (message->type == GWIPC_MESSAGE_FRAME_COMMIT) {
    gw::ipc::wire::FrameCommit commit;
    if (gw::ipc::wire::decode(payload, commit) !=
        gw::ipc::wire::CodecStatus::Ok)
      return GWIPC_STATUS_PROTOCOL_ERROR;
    if (connection->pending_frame_commits.size() >=
        connection->config.maximum_queued_messages)
      return GWIPC_STATUS_LIMIT_EXCEEDED;
    connection->pending_frame_commits.emplace(pending_sequence,
                                              commit.commit_id);
    frame_commit_inserted = true;
  }
  if (message->type == GWIPC_MESSAGE_FRAME_ACKNOWLEDGED) {
    gw::ipc::wire::FrameAcknowledged acknowledged;
    const auto expected = connection->incoming_frame_commits.find(
        message->reply_to);
    if (gw::ipc::wire::decode(payload, acknowledged) !=
            gw::ipc::wire::CodecStatus::Ok ||
        expected == connection->incoming_frame_commits.end() ||
        expected->second != acknowledged.commit_id)
      return GWIPC_STATUS_INVALID_STATE;
    frame_acknowledges_incoming = true;
  }
  if (message->type == GWIPC_MESSAGE_POLICY_COMMIT) {
    gw::ipc::wire::PolicyCommit commit;
    if (gw::ipc::wire::decode(payload, commit) !=
        gw::ipc::wire::CodecStatus::Ok)
      return GWIPC_STATUS_PROTOCOL_ERROR;
    if (connection->pending_policy_commits.size() >=
        connection->config.maximum_queued_messages)
      return GWIPC_STATUS_LIMIT_EXCEEDED;
    connection->pending_policy_commits.emplace(pending_sequence,
                                               commit.commit_id);
    policy_commit_inserted = true;
  }
  if (message->type == GWIPC_MESSAGE_POLICY_ACKNOWLEDGED) {
    gw::ipc::wire::PolicyAcknowledged acknowledged;
    const auto expected =
        connection->incoming_policy_commits.find(message->reply_to);
    if (gw::ipc::wire::decode(payload, acknowledged) !=
            gw::ipc::wire::CodecStatus::Ok ||
        expected == connection->incoming_policy_commits.end() ||
        expected->second != acknowledged.commit_id)
      return GWIPC_STATUS_INVALID_STATE;
    policy_acknowledges_incoming = true;
  }
  gwipc_status status = GWIPC_STATUS_SYSTEM_ERROR;
  try {
    status = gw::ipc::queue_internal(*connection, message->type,
                                     message->flags, message->reply_to, payload,
                                     fds);
  } catch (...) {
    if (ping_inserted)
      connection->pending_ping_nonces.erase(pending_sequence);
    if (frame_commit_inserted)
      connection->pending_frame_commits.erase(pending_sequence);
    if (policy_commit_inserted)
      connection->pending_policy_commits.erase(pending_sequence);
    throw;
  }
  if (status == GWIPC_STATUS_OK) {
    connection->outgoing_snapshot = snapshot;
    if (frame_acknowledges_incoming)
      connection->incoming_frame_commits.erase(message->reply_to);
    if (policy_acknowledges_incoming)
      connection->incoming_policy_commits.erase(message->reply_to);
  } else if (ping_inserted) {
    connection->pending_ping_nonces.erase(pending_sequence);
  }
  if (status != GWIPC_STATUS_OK && frame_commit_inserted)
    connection->pending_frame_commits.erase(pending_sequence);
  if (status != GWIPC_STATUS_OK && policy_commit_inserted)
    connection->pending_policy_commits.erase(pending_sequence);
  return status;
  } catch (const std::bad_alloc&) {
    return GWIPC_STATUS_OUT_OF_MEMORY;
  } catch (...) {
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
}

gwipc_status gwipc_connection_receive(gwipc_connection* connection,
                                      gwipc_message** out_message) {
  if (!connection || !out_message) return GWIPC_STATUS_INVALID_ARGUMENT;
  *out_message = nullptr;
  if (connection->incoming.empty())
    return connection->state == GWIPC_CONNECTION_CLOSED
               ? GWIPC_STATUS_DISCONNECTED
               : GWIPC_STATUS_WOULD_BLOCK;
  *out_message = connection->incoming.front();
  connection->incoming.pop_front();
  return GWIPC_STATUS_OK;
}

}  // extern "C"
