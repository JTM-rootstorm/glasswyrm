#include "ipc/connection_internal.hpp"

#include <fcntl.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <span>
#include <vector>

namespace gw::ipc {
namespace {

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

bool queue_limit_exceeded(const gwipc_connection& connection,
                          std::size_t record_size) noexcept {
  return connection.outgoing.size() >=
             connection.config.maximum_queued_messages ||
         record_size >
             connection.config.maximum_queued_bytes -
                 std::min<std::size_t>(connection.queued_bytes,
                                       connection.config.maximum_queued_bytes);
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
  if (queue_limit_exceeded(connection, payload.size() + wire::kEnvelopeSize)) {
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
  envelope.type = static_cast<wire::MessageType>(type);
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
    if (expects_reply)
      connection.pending_replies.erase(connection.next_send_sequence);
    throw;
  }
  ++connection.next_send_sequence;
  connection.queued_bytes += record_size;
  return GWIPC_STATUS_OK;
}

}  // namespace gw::ipc
