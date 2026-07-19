#include "ipc/connection_internal.hpp"

#include "ipc/wire/compositor_contract.hpp"
#include "ipc/wire/control.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <vector>

namespace gw::ipc {

bool output_extension_message(std::uint16_t type) noexcept;

namespace {

using wire::MessageFlag;
using wire::MessageType;

struct ReceivedRecord {
  std::vector<std::uint8_t> bytes;
  std::vector<int> fds;
  bool available{};

  ~ReceivedRecord() {
    for (auto& fd : fds) close_fd(fd);
  }
};

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
    case GWIPC_MESSAGE_OUTPUT_DESCRIPTOR_UPSERT:
    case GWIPC_MESSAGE_OUTPUT_MODE_UPSERT:
    case GWIPC_MESSAGE_OUTPUT_VRR_CAPABILITY_UPSERT:
    case GWIPC_MESSAGE_OUTPUT_VRR_POLICY_UPSERT:
    case GWIPC_MESSAGE_OUTPUT_VRR_STATE_UPSERT:
    case GWIPC_MESSAGE_SURFACE_UPSERT:
    case GWIPC_MESSAGE_SURFACE_REMOVE:
    case GWIPC_MESSAGE_SURFACE_POLICY_UPSERT:
    case GWIPC_MESSAGE_SURFACE_OUTPUT_STATE:
    case GWIPC_MESSAGE_SURFACE_VRR_STATE:
    case GWIPC_MESSAGE_BUFFER_ATTACH:
    case GWIPC_MESSAGE_BUFFER_DETACH:
    case GWIPC_MESSAGE_BUFFER_RELEASE:
    case GWIPC_MESSAGE_SURFACE_DAMAGE:
    case GWIPC_MESSAGE_FRAME_COMMIT:
    case GWIPC_MESSAGE_FRAME_ACKNOWLEDGED:
    case GWIPC_MESSAGE_PRESENTATION_TIMING:
    case GWIPC_MESSAGE_POLICY_CONTEXT_UPSERT:
    case GWIPC_MESSAGE_POLICY_WINDOW_UPSERT:
    case GWIPC_MESSAGE_POLICY_WINDOW_REMOVE:
    case GWIPC_MESSAGE_POLICY_LIFECYCLE_WINDOW_UPSERT:
    case GWIPC_MESSAGE_POLICY_OUTPUT_UPSERT:
    case GWIPC_MESSAGE_POLICY_WINDOW_OUTPUT_HINT:
    case GWIPC_MESSAGE_POLICY_WINDOW_VRR_UPSERT:
    case GWIPC_MESSAGE_POLICY_OUTPUT_VRR_UPSERT:
    case GWIPC_MESSAGE_POLICY_COMMIT:
    case GWIPC_MESSAGE_POLICY_WINDOW_STATE:
    case GWIPC_MESSAGE_POLICY_ACKNOWLEDGED:
    case GWIPC_MESSAGE_POLICY_BINDINGS_UPSERT:
    case GWIPC_MESSAGE_POLICY_WINDOW_VRR_STATE:
    case GWIPC_MESSAGE_POLICY_OUTPUT_VRR_STATE:
    case GWIPC_MESSAGE_SYNTHETIC_MOTION:
    case GWIPC_MESSAGE_SYNTHETIC_BUTTON:
    case GWIPC_MESSAGE_SYNTHETIC_KEY:
    case GWIPC_MESSAGE_SYNTHETIC_BARRIER:
    case GWIPC_MESSAGE_SYNTHETIC_INPUT_ACKNOWLEDGED:
    case GWIPC_MESSAGE_SESSION_STATE_CHANGE:
    case GWIPC_MESSAGE_SESSION_STATE_ACKNOWLEDGED:
    case GWIPC_MESSAGE_OUTPUT_STATE_QUERY:
    case GWIPC_MESSAGE_OUTPUT_CONFIGURATION_COMMIT:
    case GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED:
      return true;
    default:
      return false;
  }
}

void collect_descriptors(msghdr& message, ReceivedRecord& record,
                         bool& invalid) {
  invalid = (message.msg_flags & MSG_CTRUNC) != 0;
  for (auto* header = CMSG_FIRSTHDR(&message); header;
       header = CMSG_NXTHDR(&message, header)) {
    if (header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS ||
        header->cmsg_len < CMSG_LEN(0)) {
      invalid = true;
      continue;
    }
    const auto descriptor_bytes = header->cmsg_len - CMSG_LEN(0);
    if (descriptor_bytes % sizeof(int) != 0) {
      invalid = true;
      continue;
    }
    const auto count = descriptor_bytes / sizeof(int);
    const auto* descriptors = reinterpret_cast<const int*>(CMSG_DATA(header));
    for (std::size_t index = 0; index < count; ++index) {
      if (record.fds.size() < GWIPC_HARD_MAXIMUM_FDS)
        record.fds.push_back(descriptors[index]);
      else {
        int excess = descriptors[index];
        close_fd(excess);
        invalid = true;
      }
    }
  }
}

gwipc_status decode_record(gwipc_connection& connection,
                           ReceivedRecord& record, ssize_t received,
                           const msghdr& message, bool ancillary_invalid,
                           wire::Envelope& envelope) {
  const auto maximum_payload = connection.state == GWIPC_CONNECTION_ESTABLISHED
                                   ? connection.peer.maximum_payload
                                   : connection.config.maximum_payload;
  const auto maximum_fds = connection.state == GWIPC_CONNECTION_ESTABLISHED
                               ? connection.peer.maximum_fd_count
                               : connection.config.maximum_fd_count;
  const auto record_size = std::min<std::size_t>(
      static_cast<std::size_t>(received), record.bytes.size());
  wire::Envelope error_context;
  const bool has_error_context = parse_error_context(
      std::span(record.bytes).first(record_size), error_context);
  if (ancillary_invalid || (message.msg_flags & MSG_TRUNC) != 0 ||
      static_cast<std::size_t>(received) > record.bytes.size() ||
      record.fds.size() > maximum_fds) {
    if (has_error_context) {
      const auto code = ancillary_invalid || record.fds.size() > maximum_fds
                            ? wire::ProtocolErrorCode::InvalidDescriptorCount
                            : wire::ProtocolErrorCode::LimitExceeded;
      return protocol_failure(connection, code, error_context,
                              "record or descriptor limit exceeded");
    }
    set_closed(connection);
    return GWIPC_STATUS_PROTOCOL_ERROR;
  }
  record.bytes.resize(static_cast<std::size_t>(received));
  const auto status = wire::decode_envelope(record.bytes, record.fds.size(),
                                            maximum_payload, envelope);
  if (status != wire::CodecStatus::Ok) {
    if (has_error_context) {
      const auto code = error_context.fd_count != record.fds.size()
                            ? wire::ProtocolErrorCode::InvalidDescriptorCount
                        : status == wire::CodecStatus::LimitExceeded
                            ? wire::ProtocolErrorCode::LimitExceeded
                            : wire::ProtocolErrorCode::MalformedEnvelope;
      return protocol_failure(connection, code, error_context,
                              "malformed message envelope");
    }
    set_closed(connection);
    return GWIPC_STATUS_PROTOCOL_ERROR;
  }
  if (envelope.sequence != connection.next_receive_sequence)
    return protocol_failure(connection,
                            wire::ProtocolErrorCode::OutOfOrderSequence,
                            envelope, "out-of-order sequence");
  if (connection.next_receive_sequence == UINT64_MAX) {
    set_closed(connection);
    return GWIPC_STATUS_LIMIT_EXCEEDED;
  }
  ++connection.next_receive_sequence;
  return GWIPC_STATUS_OK;
}

gwipc_status receive_record(gwipc_connection& connection,
                            ReceivedRecord& record,
                            wire::Envelope& envelope) {
  const auto maximum_payload = connection.state == GWIPC_CONNECTION_ESTABLISHED
                                   ? connection.peer.maximum_payload
                                   : connection.config.maximum_payload;
  try {
    record.bytes.resize(wire::kEnvelopeSize + maximum_payload);
    record.fds.reserve(GWIPC_HARD_MAXIMUM_FDS);
  } catch (...) {
    return GWIPC_STATUS_OUT_OF_MEMORY;
  }
  std::array<std::byte, CMSG_SPACE(sizeof(int) * GWIPC_HARD_MAXIMUM_FDS)>
      control{};
  iovec vector{record.bytes.data(), record.bytes.size()};
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
  record.available = true;
  bool ancillary_invalid = false;
  collect_descriptors(message, record, ancillary_invalid);
  return decode_record(connection, record, received, message,
                       ancillary_invalid, envelope);
}

gwipc_status unsupported_message(gwipc_connection& connection,
                                 const wire::Envelope& envelope) {
  std::fprintf(stderr,
               "gwipc: protocol error code=UnsupportedMessage type=%u sequence=%llu\n",
               static_cast<unsigned>(envelope.type),
               static_cast<unsigned long long>(envelope.sequence));
  wire::ProtocolError error;
  error.code = wire::ProtocolErrorCode::UnsupportedMessage;
  error.offending_type = envelope.type;
  error.offending_sequence = envelope.sequence;
  error.detail = "unsupported message type";
  const auto payload = wire::encode(error);
  const auto queued = queue_internal(
      connection, GWIPC_MESSAGE_PROTOCOL_ERROR,
      GWIPC_FLAG_REPLY | GWIPC_FLAG_ERROR, envelope.sequence, payload);
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

gwipc_status validate_established(gwipc_connection& connection,
                                  const wire::Envelope& envelope,
                                  std::span<const std::uint8_t> payload,
                                  std::span<const int> fds) {
  if (envelope.major != connection.peer.wire_version.major ||
      envelope.minor != connection.peer.wire_version.minor ||
      envelope.type == MessageType::Hello ||
      envelope.type == MessageType::Welcome ||
      envelope.type == MessageType::Reject)
    return protocol_failure(connection,
                            wire::ProtocolErrorCode::MalformedEnvelope,
                            envelope, "invalid established message envelope");
  if (wire::has_flag(envelope.flags, MessageFlag::Reply)) {
    const auto status = validate_incoming_reply(connection, envelope, payload);
    if (status != GWIPC_STATUS_OK) return status;
  }
  if (envelope.type == MessageType::Ping) {
    wire::Ping ping;
    if (envelope.flags != GWIPC_FLAG_ACK_REQUIRED || envelope.reply_to != 0 ||
        !fds.empty() || wire::decode(payload, ping) != wire::CodecStatus::Ok)
      return protocol_failure(
          connection,
          !fds.empty() ? wire::ProtocolErrorCode::InvalidDescriptorCount
                       : wire::ProtocolErrorCode::MalformedPayload,
          envelope, "invalid ping message");
    const auto pong = wire::encode(wire::Pong{ping.nonce});
    return queue_internal(connection, GWIPC_MESSAGE_PONG, GWIPC_FLAG_REPLY,
                          envelope.sequence, pong);
  }
  if (!supported_established_type(static_cast<std::uint16_t>(envelope.type)))
    return unsupported_message(connection, envelope);
  const auto status = validate_application(
      connection, static_cast<std::uint16_t>(envelope.type), envelope.flags,
      payload, fds, connection.incoming_snapshot,
      MessageDirection::Incoming);
  if (status == GWIPC_STATUS_OK) return GWIPC_STATUS_OK;
  auto code = wire::ProtocolErrorCode::MalformedPayload;
  if (status == GWIPC_STATUS_CAPABILITY_MISMATCH)
    code = wire::ProtocolErrorCode::MissingCapability;
  else if (output_extension_message(
               static_cast<std::uint16_t>(envelope.type)) &&
           !fds.empty())
    code = wire::ProtocolErrorCode::InvalidDescriptorCount;
  else if (snapshot_control(static_cast<std::uint16_t>(envelope.type)) ||
           wire::has_flag(envelope.flags, MessageFlag::SnapshotItem))
    code = wire::ProtocolErrorCode::SnapshotViolation;
  else if (envelope.type == MessageType::BufferAttach) {
    wire::BufferAttach attachment;
    const auto decoded = wire::decode(payload, attachment);
    const std::size_t expected =
        decoded == wire::CodecStatus::Ok &&
                attachment.synchronization ==
                    wire::SynchronizationMode::EventFd
            ? 2U
            : 1U;
    code = fds.size() == expected
               ? wire::ProtocolErrorCode::InvalidDescriptor
               : wire::ProtocolErrorCode::InvalidDescriptorCount;
  }
  return protocol_failure(connection, code, envelope,
                          "invalid application message", status);
}

gwipc_status queue_received(gwipc_connection& connection,
                            const wire::Envelope& envelope,
                            std::span<const std::uint8_t> payload,
                            ReceivedRecord& record) {
  auto status = track_incoming_request(connection, envelope, payload);
  if (status != GWIPC_STATUS_OK) return status;
  if (connection.incoming.size() >= connection.config.maximum_queued_messages) {
    rollback_incoming_request(connection, envelope);
    return protocol_failure(connection, wire::ProtocolErrorCode::LimitExceeded,
                            envelope, "incoming queue limit exceeded",
                            GWIPC_STATUS_LIMIT_EXCEEDED);
  }
  auto message = std::unique_ptr<gwipc_message>(new (std::nothrow) gwipc_message);
  if (!message) {
    rollback_incoming_request(connection, envelope);
    return GWIPC_STATUS_OUT_OF_MEMORY;
  }
  message->type = static_cast<std::uint16_t>(envelope.type);
  message->flags = envelope.flags;
  message->sequence = envelope.sequence;
  message->reply_to = envelope.reply_to;
  try {
    message->payload.assign(payload.begin(), payload.end());
    message->fds = std::move(record.fds);
    connection.incoming.push_back(message.get());
    message.release();
    commit_incoming_request(connection, envelope, payload);
  } catch (...) {
    rollback_incoming_request(connection, envelope);
    return GWIPC_STATUS_OUT_OF_MEMORY;
  }
  return GWIPC_STATUS_OK;
}

}  // namespace

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
                              const char* detail, gwipc_status result) {
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
  ReceivedRecord record;
  wire::Envelope envelope;
  const auto status = receive_record(connection, record, envelope);
  if (status != GWIPC_STATUS_OK || !record.available) return status;
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
  const auto payload = std::span(record.bytes).subspan(wire::kEnvelopeSize);
  if (connection.state == GWIPC_CONNECTION_AWAITING_HELLO)
    return handle_hello(connection, envelope, payload);
  if (connection.state == GWIPC_CONNECTION_AWAITING_WELCOME)
    return handle_welcome(connection, envelope, payload);
  if (connection.state != GWIPC_CONNECTION_ESTABLISHED)
    return GWIPC_STATUS_INVALID_STATE;
  const auto validated = validate_established(connection, envelope, payload,
                                              record.fds);
  if (validated != GWIPC_STATUS_OK || envelope.type == MessageType::Ping ||
      !supported_established_type(static_cast<std::uint16_t>(envelope.type)))
    return validated;
  return queue_received(connection, envelope, payload, record);
}

}  // namespace gw::ipc
