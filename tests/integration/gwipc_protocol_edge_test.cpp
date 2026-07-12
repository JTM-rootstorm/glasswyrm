#include <glasswyrm/ipc.h>

#include "ipc/internal.hpp"
#include "ipc/wire/compositor_contract.hpp"
#include "ipc/wire/control.hpp"
#include "ipc/wire/envelope.hpp"
#include "ipc/wire/policy_contract.hpp"
#include "tests/helpers/test_support.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <span>
#include <vector>

namespace {

using gw::test::require;

constexpr auto kCapabilities =
    GWIPC_CAP_FD_PASSING | GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE |
    GWIPC_CAP_MEMFD_BUFFERS | GWIPC_CAP_WINDOW_POLICY;

struct RawPair {
  gwipc_connection* connection{new gwipc_connection};
  int peer{-1};

  explicit RawPair(bool stream = false) {
    int sockets[2] = {-1, -1};
    require(::socketpair(AF_UNIX,
                         (stream ? SOCK_STREAM : SOCK_SEQPACKET) |
                             SOCK_NONBLOCK | SOCK_CLOEXEC,
                         0,
                         sockets) == 0,
            "create transport socketpair");
    connection->fd = sockets[0];
    peer = sockets[1];
    connection->state = GWIPC_CONNECTION_ESTABLISHED;
    connection->config.maximum_payload = GWIPC_DEFAULT_MAXIMUM_PAYLOAD;
    connection->config.maximum_fd_count = GWIPC_DEFAULT_MAXIMUM_FDS;
    connection->config.maximum_queued_bytes =
        GWIPC_DEFAULT_MAXIMUM_QUEUED_BYTES;
    connection->config.maximum_queued_messages =
        GWIPC_DEFAULT_MAXIMUM_QUEUED_MESSAGES;
    connection->peer.wire_version = {1, 0};
    connection->peer.capabilities = kCapabilities;
    connection->peer.maximum_payload = GWIPC_DEFAULT_MAXIMUM_PAYLOAD;
    connection->peer.maximum_fd_count = GWIPC_DEFAULT_MAXIMUM_FDS;
  }

  RawPair(const RawPair&) = delete;
  RawPair& operator=(const RawPair&) = delete;

  ~RawPair() {
    if (peer >= 0) (void)::close(peer);
    gwipc_connection_destroy(connection);
  }
};

gwipc_status enqueue(gwipc_connection* connection, std::uint16_t type,
                     std::uint32_t flags,
                     std::span<const std::uint8_t> payload,
                     std::uint64_t reply_to = 0, const int* fds = nullptr,
                     std::size_t fd_count = 0) {
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.flags = flags;
  message.reply_to = reply_to;
  message.payload = payload.data();
  message.payload_size = payload.size();
  message.fds = fds;
  message.fd_count = fd_count;
  return gwipc_connection_enqueue(connection, &message);
}

void send_record(int socket, std::uint64_t sequence, std::uint16_t type,
                 std::uint32_t flags,
                 std::span<const std::uint8_t> payload = {},
                 std::uint64_t reply_to = 0) {
  gw::ipc::wire::Envelope envelope;
  envelope.type = static_cast<gw::ipc::wire::MessageType>(type);
  envelope.flags = flags;
  envelope.payload_size = static_cast<std::uint32_t>(payload.size());
  envelope.sequence = sequence;
  envelope.reply_to = reply_to;
  const auto header = gw::ipc::wire::encode_envelope(envelope);
  std::vector<std::uint8_t> record(header.begin(), header.end());
  record.insert(record.end(), payload.begin(), payload.end());
  iovec vector{record.data(), record.size()};
  msghdr message{};
  message.msg_iov = &vector;
  message.msg_iovlen = 1;
  const auto sent = ::sendmsg(socket, &message, MSG_NOSIGNAL);
  if (sent != static_cast<ssize_t>(record.size()))
    std::fprintf(stderr,
                 "send record type=0x%04x sequence=%llu size=%zu sent=%zd errno=%d\n",
                 type, static_cast<unsigned long long>(sequence), record.size(),
                 sent, errno);
  require(sent == static_cast<ssize_t>(record.size()),
          "send wire record");
}

void test_policy_commit_correlation() {
  RawPair pair;
  const auto commit = gw::ipc::wire::encode(
      gw::ipc::wire::PolicyCommit{41, 7, 0});
  require(enqueue(pair.connection, GWIPC_MESSAGE_POLICY_COMMIT,
                  GWIPC_FLAG_ACK_REQUIRED, commit) == GWIPC_STATUS_OK &&
              pair.connection->pending_policy_commits.at(1) == 41,
          "outgoing policy commit records correlation identity");
  const auto acknowledged = gw::ipc::wire::encode(
      gw::ipc::wire::PolicyAcknowledged{
          41, 7, 8, 9, 2, gw::ipc::wire::PolicyResult::Accepted});
  send_record(pair.peer, 1, GWIPC_MESSAGE_POLICY_ACKNOWLEDGED,
              GWIPC_FLAG_REPLY, acknowledged, 1);
  require(gwipc_connection_process_poll_events(pair.connection, POLLIN) ==
                  GWIPC_STATUS_OK &&
              pair.connection->pending_policy_commits.empty(),
          "matching policy acknowledgement clears correlation state");

  RawPair incoming;
  const auto incoming_commit = gw::ipc::wire::encode(
      gw::ipc::wire::PolicyCommit{55, 9, 0});
  send_record(incoming.peer, 1, GWIPC_MESSAGE_POLICY_COMMIT,
              GWIPC_FLAG_ACK_REQUIRED, incoming_commit);
  require(gwipc_connection_process_poll_events(incoming.connection, POLLIN) ==
                  GWIPC_STATUS_OK &&
              incoming.connection->incoming_policy_commits.at(1) == 55,
          "incoming policy commit records reply identity");
  const auto reply = gw::ipc::wire::encode(
      gw::ipc::wire::PolicyAcknowledged{
          55, 9, 10, 11, 1, gw::ipc::wire::PolicyResult::Accepted});
  require(enqueue(incoming.connection, GWIPC_MESSAGE_POLICY_ACKNOWLEDGED,
                  GWIPC_FLAG_REPLY, reply, 1) == GWIPC_STATUS_OK &&
              incoming.connection->incoming_policy_commits.empty(),
          "outgoing policy acknowledgement matches incoming commit");

  RawPair mismatch;
  require(enqueue(mismatch.connection, GWIPC_MESSAGE_POLICY_COMMIT,
                  GWIPC_FLAG_ACK_REQUIRED, commit) == GWIPC_STATUS_OK,
          "queue policy commit before mismatched reply");
  const auto wrong = gw::ipc::wire::encode(
      gw::ipc::wire::PolicyAcknowledged{
          42, 7, 8, 9, 2, gw::ipc::wire::PolicyResult::Accepted});
  send_record(mismatch.peer, 1, GWIPC_MESSAGE_POLICY_ACKNOWLEDGED,
              GWIPC_FLAG_REPLY, wrong, 1);
  require(gwipc_connection_process_poll_events(mismatch.connection, POLLIN) ==
              GWIPC_STATUS_PROTOCOL_ERROR,
          "mismatched policy acknowledgement is rejected");
}

void test_outgoing_snapshot_violations_are_atomic() {
  RawPair pair;
  const auto begin = gw::ipc::wire::encode(gw::ipc::wire::SnapshotBegin{
      17, gw::ipc::wire::SnapshotDomain::Test, 0, 23, 1});
  require(enqueue(pair.connection, GWIPC_MESSAGE_SNAPSHOT_BEGIN, 0, begin) ==
              GWIPC_STATUS_OK,
          "begin outgoing snapshot");

  const auto queued = pair.connection->outgoing.size();
  const auto sequence = pair.connection->next_send_sequence;
  require(enqueue(pair.connection, GWIPC_MESSAGE_SNAPSHOT_BEGIN, 0, begin) ==
              GWIPC_STATUS_PROTOCOL_ERROR,
          "nested outgoing snapshot is rejected");

  const auto wrong_generation =
      gw::ipc::wire::encode(gw::ipc::wire::SnapshotEnd{17, 24, 0});
  require(enqueue(pair.connection, GWIPC_MESSAGE_SNAPSHOT_END, 0,
                  wrong_generation) == GWIPC_STATUS_PROTOCOL_ERROR,
          "wrong snapshot generation is rejected");

  const auto wrong_count =
      gw::ipc::wire::encode(gw::ipc::wire::SnapshotEnd{17, 23, 0});
  require(enqueue(pair.connection, GWIPC_MESSAGE_SNAPSHOT_END, 0,
                  wrong_count) == GWIPC_STATUS_PROTOCOL_ERROR,
          "wrong snapshot item count is rejected");
  require(pair.connection->outgoing_snapshot.active &&
              pair.connection->outgoing_snapshot.id == 17 &&
              pair.connection->outgoing_snapshot.generation == 23 &&
              pair.connection->outgoing_snapshot.item_count == 0 &&
              pair.connection->outgoing.size() == queued &&
              pair.connection->next_send_sequence == sequence,
          "snapshot violations preserve queue and snapshot state");
}

void test_incoming_snapshot_violation_closes_after_error() {
  RawPair pair;
  const auto begin = gw::ipc::wire::encode(gw::ipc::wire::SnapshotBegin{
      5, gw::ipc::wire::SnapshotDomain::Test, 0, 9, 1});
  send_record(pair.peer, 1, GWIPC_MESSAGE_SNAPSHOT_BEGIN, 0, begin);
  require(gwipc_connection_process_poll_events(pair.connection, POLLIN) ==
              GWIPC_STATUS_OK,
          "receive snapshot begin");

  const auto wrong_end =
      gw::ipc::wire::encode(gw::ipc::wire::SnapshotEnd{5, 10, 0});
  send_record(pair.peer, 2, GWIPC_MESSAGE_SNAPSHOT_END, 0, wrong_end);
  require(gwipc_connection_process_poll_events(pair.connection, POLLIN) ==
              GWIPC_STATUS_PROTOCOL_ERROR,
          "incoming snapshot violation is reported");
  require(gwipc_connection_get_state(pair.connection) ==
                  GWIPC_CONNECTION_CLOSING &&
              !pair.connection->outgoing.empty(),
          "snapshot violation queues a protocol error before closing");
  require(gwipc_connection_process_poll_events(pair.connection, POLLOUT) ==
                  GWIPC_STATUS_OK &&
              gwipc_connection_get_state(pair.connection) ==
                  GWIPC_CONNECTION_CLOSED &&
              gwipc_connection_snapshot_aborted(pair.connection) != 0,
          "snapshot violation flushes its error and marks the snapshot aborted");
}

void test_disconnect_marks_both_snapshot_directions_aborted() {
  {
    RawPair pair;
    const auto begin = gw::ipc::wire::encode(gw::ipc::wire::SnapshotBegin{
        1, gw::ipc::wire::SnapshotDomain::Test, 0, 2, UINT32_MAX});
    require(enqueue(pair.connection, GWIPC_MESSAGE_SNAPSHOT_BEGIN, 0, begin) ==
                GWIPC_STATUS_OK,
            "begin outgoing snapshot before disconnect");
    (void)::close(pair.peer);
    pair.peer = -1;
    const auto status = gwipc_connection_process_poll_events(
        pair.connection, POLLIN | POLLOUT | POLLHUP);
    require(status == GWIPC_STATUS_DISCONNECTED ||
                status == GWIPC_STATUS_SYSTEM_ERROR,
            "outgoing snapshot peer disconnect is reported");
    require(gwipc_connection_snapshot_aborted(pair.connection) != 0,
            "outgoing snapshot disconnect is observable");
  }

  {
    RawPair pair;
    const auto begin = gw::ipc::wire::encode(gw::ipc::wire::SnapshotBegin{
        2, gw::ipc::wire::SnapshotDomain::Test, 0, 3, UINT32_MAX});
    send_record(pair.peer, 1, GWIPC_MESSAGE_SNAPSHOT_BEGIN, 0, begin);
    require(gwipc_connection_process_poll_events(pair.connection, POLLIN) ==
                GWIPC_STATUS_OK,
            "begin incoming snapshot before disconnect");
    (void)::close(pair.peer);
    pair.peer = -1;
    require(gwipc_connection_process_poll_events(pair.connection, POLLIN) ==
                GWIPC_STATUS_DISCONNECTED,
            "incoming snapshot peer disconnect is reported");
    require(gwipc_connection_snapshot_aborted(pair.connection) != 0,
            "incoming snapshot disconnect is observable");
  }
}

void test_unsupported_message_criticality() {
  {
    RawPair pair;
    send_record(pair.peer, 1, UINT16_C(0x7ffe), 0);
    require(gwipc_connection_process_poll_events(pair.connection, POLLIN) ==
                    GWIPC_STATUS_OK &&
                gwipc_connection_get_state(pair.connection) ==
                    GWIPC_CONNECTION_ESTABLISHED,
            "noncritical unsupported message keeps the connection alive");

    const auto valid =
        gw::ipc::wire::encode(gw::ipc::wire::OutputRemove{41});
    send_record(pair.peer, 2, GWIPC_MESSAGE_OUTPUT_REMOVE, 0, valid);
    require(gwipc_connection_process_poll_events(pair.connection, POLLIN) ==
                GWIPC_STATUS_OK,
            "valid traffic continues after a noncritical unsupported message");
    gwipc_message* received = nullptr;
    require(gwipc_connection_receive(pair.connection, &received) ==
                    GWIPC_STATUS_OK &&
                gwipc_message_type(received) == GWIPC_MESSAGE_OUTPUT_REMOVE,
            "continued traffic reaches the application");
    gwipc_message_destroy(received);
  }

  {
    RawPair pair;
    send_record(pair.peer, 1, UINT16_C(0x7ffe), GWIPC_FLAG_CRITICAL);
    require(gwipc_connection_process_poll_events(pair.connection, POLLIN) ==
                    GWIPC_STATUS_PROTOCOL_ERROR &&
                gwipc_connection_get_state(pair.connection) ==
                    GWIPC_CONNECTION_CLOSING,
            "critical unsupported message starts an error close");
    require(gwipc_connection_process_poll_events(pair.connection, POLLOUT) ==
                    GWIPC_STATUS_OK &&
                gwipc_connection_get_state(pair.connection) ==
                    GWIPC_CONNECTION_CLOSED,
            "critical unsupported message closes after flushing its error");
  }
}

void test_half_close_drains_queued_output() {
  RawPair pair(true);
  const auto payload =
      gw::ipc::wire::encode(gw::ipc::wire::OutputRemove{88});
  require(enqueue(pair.connection, GWIPC_MESSAGE_OUTPUT_REMOVE, 0, payload) ==
              GWIPC_STATUS_OK,
          "queue output before peer half-close");
  require(gwipc_connection_process_poll_events(pair.connection,
                                                POLLOUT | POLLHUP) ==
              GWIPC_STATUS_DISCONNECTED,
          "half-close poll state is reported after draining");

  std::vector<std::uint8_t> record(gw::ipc::wire::kEnvelopeSize + payload.size());
  require(::recv(pair.peer, record.data(), record.size(), 0) ==
              static_cast<ssize_t>(record.size()),
          "queued record survives peer half-close");
}

void test_message_descriptor_ownership() {
  const int unclaimed = ::memfd_create("gwipc-unclaimed", MFD_CLOEXEC);
  require(unclaimed >= 0, "create unclaimed descriptor");
  auto* message = new gwipc_message;
  message->fds.push_back(unclaimed);
  require(gwipc_message_fd_count(message) == 1,
          "message reports its unclaimed descriptor");
  gwipc_message_destroy(message);
  require(::fcntl(unclaimed, F_GETFD) < 0 && errno == EBADF,
          "destroying a message closes an unclaimed descriptor");

  const int claimed = ::memfd_create("gwipc-claimed", MFD_CLOEXEC);
  require(claimed >= 0, "create claimed descriptor");
  message = new gwipc_message;
  message->fds.push_back(claimed);
  int owned = -1;
  require(gwipc_message_take_fd(message, 0, &owned) == GWIPC_STATUS_OK &&
              owned == claimed && gwipc_message_fd_count(message) == 0,
          "taking a descriptor transfers ownership exactly once");
  int repeated = -1;
  require(gwipc_message_take_fd(message, 0, &repeated) ==
                  GWIPC_STATUS_INVALID_STATE &&
              repeated == -1,
          "repeated descriptor take is rejected");
  gwipc_message_destroy(message);
  require(::fcntl(owned, F_GETFD) >= 0,
          "message destruction preserves a claimed descriptor");
  (void)::close(owned);
}

}  // namespace

int main() {
  test_policy_commit_correlation();
  test_outgoing_snapshot_violations_are_atomic();
  test_incoming_snapshot_violation_closes_after_error();
  test_disconnect_marks_both_snapshot_directions_aborted();
  test_unsupported_message_criticality();
  test_half_close_drains_queued_output();
  test_message_descriptor_ownership();
  return 0;
}
