#include <glasswyrm/ipc.h>

#include "ipc/internal.hpp"
#include "ipc/wire/compositor_contract.hpp"
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
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
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
      GWIPC_CAP_OUTPUT_STATE;
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

}  // namespace

int main() {
  test_output_cap_is_atomic();
  test_fd_duplication_failure_is_atomic();
  test_send_sequence_exhaustion();
  test_receive_sequence_exhaustion();
  test_connection_id_exhaustion();
  return 0;
}
