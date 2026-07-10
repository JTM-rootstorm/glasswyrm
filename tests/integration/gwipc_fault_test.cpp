#include <glasswyrm/ipc.h>

#include "ipc/internal.hpp"
#include "ipc/wire/compositor_contract.hpp"
#include "ipc/wire/control.hpp"
#include "ipc/wire/envelope.hpp"
#include "tests/helpers/test_support.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <span>
#include <vector>

namespace {

using gw::test::require;

std::size_t open_fd_count() {
  DIR* directory = ::opendir("/proc/self/fd");
  require(directory != nullptr, "open /proc/self/fd");
  std::size_t count = 0;
  while (const auto* entry = ::readdir(directory)) {
    if (std::strcmp(entry->d_name, ".") != 0 &&
        std::strcmp(entry->d_name, "..") != 0) {
      ++count;
    }
  }
  require(::closedir(directory) == 0, "close /proc/self/fd");
  return count;
}

gwipc_connection* established_connection() {
  auto* connection = new gwipc_connection;
  connection->state = GWIPC_CONNECTION_ESTABLISHED;
  connection->config.maximum_payload = GWIPC_DEFAULT_MAXIMUM_PAYLOAD;
  connection->config.maximum_fd_count = GWIPC_DEFAULT_MAXIMUM_FDS;
  connection->config.maximum_queued_bytes =
      GWIPC_DEFAULT_MAXIMUM_QUEUED_BYTES;
  connection->config.maximum_queued_messages =
      GWIPC_DEFAULT_MAXIMUM_QUEUED_MESSAGES;
  connection->peer.wire_version = {1, 0};
  connection->peer.capabilities =
      GWIPC_CAP_FD_PASSING | GWIPC_CAP_MEMFD_BUFFERS |
      GWIPC_CAP_OUTPUT_STATE | GWIPC_CAP_SURFACE_STATE |
      GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_DAMAGE_REGIONS |
      GWIPC_CAP_FRAME_ACKNOWLEDGEMENT;
  connection->peer.maximum_payload = GWIPC_DEFAULT_MAXIMUM_PAYLOAD;
  connection->peer.maximum_fd_count = GWIPC_DEFAULT_MAXIMUM_FDS;
  return connection;
}

gwipc_status enqueue(gwipc_connection* connection, std::uint16_t type,
                     std::uint32_t flags,
                     const std::vector<std::uint8_t>& payload,
                     const int* fds = nullptr, std::size_t fd_count = 0) {
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.flags = flags;
  message.payload = payload.data();
  message.payload_size = payload.size();
  message.fds = fds;
  message.fd_count = fd_count;
  return gwipc_connection_enqueue(connection, &message);
}

void test_output_cap_is_atomic() {
  auto* connection = established_connection();
  connection->config.maximum_queued_bytes = 47;
  const auto payload =
      gw::ipc::wire::encode(gw::ipc::wire::OutputRemove{1});
  require(enqueue(connection, GWIPC_MESSAGE_OUTPUT_REMOVE, 0, payload) ==
              GWIPC_STATUS_LIMIT_EXCEEDED,
          "output cap rejects a complete 48-byte record");
  require(connection->state == GWIPC_CONNECTION_CLOSED &&
              connection->outgoing.empty() && connection->queued_bytes == 0 &&
              connection->next_send_sequence == 1,
          "output cap closes without consuming sequence or queue state");
  gwipc_connection_destroy(connection);
}

struct RlimitGuard {
  rlimit original{};
  ~RlimitGuard() { (void)::setrlimit(RLIMIT_NOFILE, &original); }
};

void test_fd_duplication_failure_is_atomic() {
  const auto baseline = open_fd_count();
  const int buffer_fd =
      ::memfd_create("gwipc-fault", MFD_CLOEXEC | MFD_ALLOW_SEALING);
  require(buffer_fd >= 0 && ::ftruncate(buffer_fd, 4096) == 0,
          "create buffer fd for duplication failure");

  RlimitGuard guard;
  require(::getrlimit(RLIMIT_NOFILE, &guard.original) == 0,
          "read descriptor limit");
  rlimit constrained = guard.original;
  constrained.rlim_cur = static_cast<rlim_t>(buffer_fd + 1);
  require(::setrlimit(RLIMIT_NOFILE, &constrained) == 0,
          "constrain descriptor limit");

  std::vector<int> fillers;
  for (;;) {
    const int fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
      require(errno == EMFILE, "fill descriptor table to EMFILE");
      break;
    }
    fillers.push_back(fd);
  }

  auto* connection = established_connection();
  gw::ipc::wire::BufferAttach attach;
  attach.buffer_id = 1;
  attach.surface_id = 2;
  attach.width = 8;
  attach.height = 8;
  attach.stride = 32;
  attach.storage_size = 4096;
  require(enqueue(connection, GWIPC_MESSAGE_BUFFER_ATTACH, 0,
                  gw::ipc::wire::encode(attach), &buffer_fd, 1) ==
              GWIPC_STATUS_SYSTEM_ERROR,
          "F_DUPFD_CLOEXEC failure is reported");
  require(connection->state == GWIPC_CONNECTION_ESTABLISHED &&
              connection->outgoing.empty() && connection->queued_bytes == 0 &&
              connection->next_send_sequence == 1 &&
              ::fcntl(buffer_fd, F_GETFD) >= 0,
          "duplication failure preserves caller ownership and queue state");
  gwipc_connection_destroy(connection);
  for (const int fd : fillers) (void)::close(fd);
  (void)::close(buffer_fd);
  require(::setrlimit(RLIMIT_NOFILE, &guard.original) == 0,
          "restore descriptor limit");
  require(open_fd_count() == baseline,
          "duplication failure does not leak descriptors");
}

void test_send_sequence_exhaustion() {
  auto* connection = established_connection();
  connection->next_send_sequence = UINT64_MAX;
  const auto payload =
      gw::ipc::wire::encode(gw::ipc::wire::OutputRemove{1});
  require(enqueue(connection, GWIPC_MESSAGE_OUTPUT_REMOVE, 0, payload) ==
              GWIPC_STATUS_LIMIT_EXCEEDED,
          "send sequence exhaustion is reported");
  require(connection->state == GWIPC_CONNECTION_CLOSED &&
              connection->outgoing.empty(),
          "send sequence exhaustion closes atomically");
  gwipc_connection_destroy(connection);
}

void test_receive_sequence_exhaustion() {
  int sockets[2] = {-1, -1};
  require(::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0,
                       sockets) == 0,
          "create sequence socketpair");
  auto* connection = established_connection();
  connection->fd = sockets[0];
  connection->next_receive_sequence = UINT64_MAX;

  const auto payload =
      gw::ipc::wire::encode(gw::ipc::wire::OutputRemove{1});
  gw::ipc::wire::Envelope envelope;
  envelope.type = gw::ipc::wire::MessageType::OutputRemove;
  envelope.payload_size = static_cast<std::uint32_t>(payload.size());
  envelope.sequence = UINT64_MAX;
  const auto header = gw::ipc::wire::encode_envelope(envelope);
  std::vector<std::uint8_t> record(header.begin(), header.end());
  record.insert(record.end(), payload.begin(), payload.end());
  iovec vector{record.data(), record.size()};
  msghdr message{};
  message.msg_iov = &vector;
  message.msg_iovlen = 1;
  const auto sent = ::sendmsg(sockets[1], &message, MSG_NOSIGNAL);
  if (sent < 0) std::perror("send terminal receive sequence");
  require(sent == static_cast<ssize_t>(record.size()),
          "send terminal receive sequence");
  require(gwipc_connection_process_poll_events(connection, POLLIN) ==
              GWIPC_STATUS_LIMIT_EXCEEDED,
          "receive sequence exhaustion is reported");
  require(connection->state == GWIPC_CONNECTION_CLOSED,
          "receive sequence exhaustion closes the connection");
  (void)::close(sockets[1]);
  gwipc_connection_destroy(connection);
}

void test_connection_id_exhaustion() {
  char temporary[] = "/tmp/gwipc-id-fault-XXXXXX";
  require(::mkdtemp(temporary) != nullptr, "create connection-id directory");
  const std::string path = std::string(temporary) + "/endpoint.sock";

  gwipc_listener_options options{};
  options.struct_size = sizeof(options);
  options.path = path.c_str();
  options.local_role = GWIPC_ROLE_TEST_CONSUMER;
  options.accepted_peer_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_TEST_PRODUCER);
  gwipc_listener* listener = nullptr;
  const auto create_status = gwipc_listener_create(&options, &listener);
  if (create_status != GWIPC_STATUS_OK) {
    std::fprintf(stderr, "connection-id listener: %s errno=%d\n",
                 gwipc_status_string(create_status), errno);
  }
  require(create_status == GWIPC_STATUS_OK,
          "create connection-id listener");
  listener->next_connection_id = 0;

  for (int attempt = 0; attempt < 2; ++attempt) {
    const int client =
        ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    require(client >= 0, "create connection-id client");
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    require(::connect(client, reinterpret_cast<sockaddr*>(&address),
                      static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) +
                                             path.size() + 1)) == 0,
            "connect connection-id client");
    gwipc_connection* accepted = nullptr;
    require(gwipc_listener_accept(listener, &accepted) ==
                    GWIPC_STATUS_LIMIT_EXCEEDED &&
                accepted == nullptr && listener->next_connection_id == 0,
            "connection IDs remain exhausted without wrapping");
    (void)::close(client);
  }

  gwipc_listener_destroy(listener);
  require(::rmdir(temporary) == 0, "remove connection-id directory");
}

std::vector<std::uint8_t> make_record(
    gw::ipc::wire::MessageType type, std::uint64_t sequence,
    const std::vector<std::uint8_t>& payload, std::uint32_t flags = 0,
    std::uint64_t reply_to = 0, std::uint16_t declared_fds = 0) {
  gw::ipc::wire::Envelope envelope;
  envelope.type = type;
  envelope.flags = flags;
  envelope.payload_size = static_cast<std::uint32_t>(payload.size());
  envelope.fd_count = declared_fds;
  envelope.sequence = sequence;
  envelope.reply_to = reply_to;
  const auto header = gw::ipc::wire::encode_envelope(envelope);
  std::vector<std::uint8_t> record(header.begin(), header.end());
  record.insert(record.end(), payload.begin(), payload.end());
  return record;
}

void send_record(int socket, const std::vector<std::uint8_t>& record,
                 int fd = -1) {
  iovec vector{const_cast<std::uint8_t*>(record.data()), record.size()};
  std::array<std::byte, CMSG_SPACE(sizeof(int))> control{};
  msghdr message{};
  message.msg_iov = &vector;
  message.msg_iovlen = 1;
  if (fd >= 0) {
    message.msg_control = control.data();
    message.msg_controllen = control.size();
    auto* header = CMSG_FIRSTHDR(&message);
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(header), &fd, sizeof(fd));
  }
  require(::sendmsg(socket, &message, MSG_NOSIGNAL) ==
              static_cast<ssize_t>(record.size()),
          "send protocol violation record");
}

void require_protocol_error(gwipc_connection* connection, int peer,
                            gw::ipc::wire::ProtocolErrorCode expected,
                            gwipc_status expected_status) {
  require(gwipc_connection_process_poll_events(connection, POLLIN) ==
              expected_status,
          "protocol violation status");
  require(connection->state == GWIPC_CONNECTION_CLOSING &&
              !connection->outgoing.empty(),
          "protocol error queues before close");
  require(gwipc_connection_process_poll_events(connection, POLLOUT) ==
              GWIPC_STATUS_OK &&
              connection->state == GWIPC_CONNECTION_CLOSED,
          "protocol error flushes before close");
  std::array<std::uint8_t, 512> bytes{};
  const auto received = ::recv(peer, bytes.data(), bytes.size(), 0);
  require(received > 0, "peer receives structured protocol error");
  gw::ipc::wire::Envelope envelope;
  require(gw::ipc::wire::decode_envelope(
              std::span(bytes).first(static_cast<std::size_t>(received)), 0,
              GWIPC_DEFAULT_MAXIMUM_PAYLOAD, envelope) ==
              gw::ipc::wire::CodecStatus::Ok &&
              envelope.type == gw::ipc::wire::MessageType::ProtocolError,
          "protocol error envelope");
  gw::ipc::wire::ProtocolError error;
  require(gw::ipc::wire::decode(
              std::span(bytes).subspan(
                  gw::ipc::wire::kEnvelopeSize,
                  static_cast<std::size_t>(received) -
                      gw::ipc::wire::kEnvelopeSize),
              error) == gw::ipc::wire::CodecStatus::Ok &&
              error.code == expected,
          "protocol error code");
}

template <typename Configure>
void exercise_protocol_error(const std::vector<std::uint8_t>& record,
                             gw::ipc::wire::ProtocolErrorCode expected,
                             gwipc_status status, Configure configure,
                             int sent_fd = -1) {
  int sockets[2] = {-1, -1};
  require(::socketpair(AF_UNIX,
                       SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0,
                       sockets) == 0,
          "create protocol error socketpair");
  auto* connection = established_connection();
  connection->fd = sockets[0];
  configure(*connection);
  send_record(sockets[1], record, sent_fd);
  require_protocol_error(connection, sockets[1], expected, status);
  (void)::close(sockets[1]);
  gwipc_connection_destroy(connection);
}

void test_structured_protocol_errors() {
  const auto output =
      gw::ipc::wire::encode(gw::ipc::wire::OutputRemove{1});
  auto malformed_envelope = make_record(
      gw::ipc::wire::MessageType::OutputRemove, 1, output, UINT32_C(1) << 31);
  exercise_protocol_error(
      malformed_envelope,
      gw::ipc::wire::ProtocolErrorCode::MalformedEnvelope,
      GWIPC_STATUS_PROTOCOL_ERROR, [](auto&) {});

  exercise_protocol_error(
      make_record(gw::ipc::wire::MessageType::OutputRemove, 2, output),
      gw::ipc::wire::ProtocolErrorCode::OutOfOrderSequence,
      GWIPC_STATUS_PROTOCOL_ERROR, [](auto&) {});
  exercise_protocol_error(
      make_record(gw::ipc::wire::MessageType::OutputRemove, 1, output, 0, 0,
                  1),
      gw::ipc::wire::ProtocolErrorCode::InvalidDescriptorCount,
      GWIPC_STATUS_PROTOCOL_ERROR, [](auto&) {});
  exercise_protocol_error(
      make_record(gw::ipc::wire::MessageType::OutputRemove, 1, {}),
      gw::ipc::wire::ProtocolErrorCode::MalformedPayload,
      GWIPC_STATUS_PROTOCOL_ERROR, [](auto&) {});
  exercise_protocol_error(
      make_record(gw::ipc::wire::MessageType::OutputRemove, 1, output),
      gw::ipc::wire::ProtocolErrorCode::MissingCapability,
      GWIPC_STATUS_CAPABILITY_MISMATCH,
      [](auto& connection) {
        connection.peer.capabilities &= ~GWIPC_CAP_OUTPUT_STATE;
      });
  exercise_protocol_error(
      make_record(gw::ipc::wire::MessageType::OutputRemove, 1, output,
                  GWIPC_FLAG_SNAPSHOT_ITEM),
      gw::ipc::wire::ProtocolErrorCode::SnapshotViolation,
      GWIPC_STATUS_PROTOCOL_ERROR, [](auto&) {});

  const int write_only = ::open("/tmp/gwipc-invalid-buffer", O_CREAT | O_TRUNC |
                                O_WRONLY | O_CLOEXEC, 0600);
  require(write_only >= 0 && ::ftruncate(write_only, 4096) == 0,
          "create invalid buffer descriptor");
  gw::ipc::wire::BufferAttach attach;
  attach.buffer_id = 1;
  attach.surface_id = 2;
  attach.width = 8;
  attach.height = 8;
  attach.stride = 32;
  attach.storage_size = 4096;
  exercise_protocol_error(
      make_record(gw::ipc::wire::MessageType::BufferAttach, 1,
                  gw::ipc::wire::encode(attach), 0, 0, 1),
      gw::ipc::wire::ProtocolErrorCode::InvalidDescriptor,
      GWIPC_STATUS_PROTOCOL_ERROR, [](auto&) {}, write_only);
  (void)::close(write_only);
  (void)::unlink("/tmp/gwipc-invalid-buffer");
}

void test_snapshot_abort_is_symmetric() {
  auto* connection = established_connection();
  connection->outgoing_snapshot.active = true;
  connection->state = GWIPC_CONNECTION_ESTABLISHED;
  require(gwipc_connection_process_poll_events(connection, POLLNVAL) ==
                  GWIPC_STATUS_DISCONNECTED &&
              connection->snapshot_aborted,
          "outgoing snapshot reports abort on disconnect");
  gwipc_connection_destroy(connection);
}

void test_frame_acknowledgement_correlation() {
  int sockets[2] = {-1, -1};
  require(::socketpair(AF_UNIX,
                       SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0,
                       sockets) == 0,
          "create frame correlation socketpair");
  auto* connection = established_connection();
  connection->fd = sockets[0];
  const auto commit =
      gw::ipc::wire::encode(gw::ipc::wire::FrameCommit{41, 1, 9, 0});
  require(enqueue(connection, GWIPC_MESSAGE_FRAME_COMMIT,
                  GWIPC_FLAG_ACK_REQUIRED, commit) == GWIPC_STATUS_OK &&
              connection->pending_frame_commits.at(1) == 41,
          "frame commit enters correlation table");
  require(gwipc_connection_process_poll_events(connection, POLLOUT) ==
              GWIPC_STATUS_OK,
          "flush tracked frame commit");
  std::array<std::uint8_t, 256> discard{};
  require(::recv(sockets[1], discard.data(), discard.size(), 0) > 0,
          "peer receives frame commit");

  const auto wrong_ack = gw::ipc::wire::encode(
      gw::ipc::wire::FrameAcknowledged{
          42, 1, 9, gw::ipc::wire::FrameResult::Accepted});
  send_record(sockets[1],
              make_record(gw::ipc::wire::MessageType::FrameAcknowledged, 1,
                          wrong_ack, GWIPC_FLAG_REPLY, 1));
  require_protocol_error(
      connection, sockets[1],
      gw::ipc::wire::ProtocolErrorCode::UnexpectedReply,
      GWIPC_STATUS_PROTOCOL_ERROR);
  (void)::close(sockets[1]);
  gwipc_connection_destroy(connection);

  connection = established_connection();
  connection->incoming_frame_commits.emplace(7, 55);
  const auto matching_ack = gw::ipc::wire::encode(
      gw::ipc::wire::FrameAcknowledged{
          55, 1, 9, gw::ipc::wire::FrameResult::Accepted});
  gwipc_outgoing_message reply{};
  reply.struct_size = sizeof(reply);
  reply.type = GWIPC_MESSAGE_FRAME_ACKNOWLEDGED;
  reply.flags = GWIPC_FLAG_REPLY;
  reply.reply_to = 7;
  reply.payload = matching_ack.data();
  reply.payload_size = matching_ack.size();
  require(gwipc_connection_enqueue(connection, &reply) == GWIPC_STATUS_OK &&
              connection->incoming_frame_commits.empty(),
          "matching frame acknowledgement consumes incoming commit");
  reply.reply_to = 8;
  require(gwipc_connection_enqueue(connection, &reply) ==
              GWIPC_STATUS_INVALID_STATE,
          "untracked frame acknowledgement is rejected");
  gwipc_connection_destroy(connection);
}

}  // namespace

int main() {
  test_output_cap_is_atomic();
  test_fd_duplication_failure_is_atomic();
  test_send_sequence_exhaustion();
  test_receive_sequence_exhaustion();
  test_connection_id_exhaustion();
  test_structured_protocol_errors();
  test_snapshot_abort_is_symmetric();
  test_frame_acknowledgement_correlation();
  return 0;
}
