#include <glasswyrm/ipc.h>

#include "ipc/wire/compositor_contract.hpp"
#include "ipc/wire/control.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

[[noreturn]] void fail(const char* message) {
  std::fprintf(stderr, "gwipc transport test: %s\n", message);
  std::exit(1);
}

void require(bool condition, const char* message) {
  if (!condition) fail(message);
}

constexpr auto kCapabilities =
    GWIPC_CAP_FD_PASSING | GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE |
    GWIPC_CAP_MEMFD_BUFFERS;

void drive(gwipc_connection* first, gwipc_connection* second,
           bool until_established = false) {
  for (int attempt = 0; attempt < 200; ++attempt) {
    pollfd descriptors[2] = {
        {gwipc_connection_fd(first),
         gwipc_connection_wanted_poll_events(first), 0},
        {gwipc_connection_fd(second),
         gwipc_connection_wanted_poll_events(second), 0},
    };
    const int result = ::poll(descriptors, 2, 20);
    require(result >= 0 || errno == EINTR, "poll failed");
    if (result >= 0) {
      for (int index = 0; index < 2; ++index) {
        if (descriptors[index].revents == 0) continue;
        const auto status = gwipc_connection_process_poll_events(
            index == 0 ? first : second, descriptors[index].revents);
        if (status != GWIPC_STATUS_OK && status != GWIPC_STATUS_WOULD_BLOCK) {
          std::fprintf(stderr,
                       "drive index=%d revents=%hd state0=%d state1=%d status=%s\n",
                       index, descriptors[index].revents,
                       gwipc_connection_get_state(first),
                       gwipc_connection_get_state(second),
                       gwipc_status_string(status));
        }
        require(status == GWIPC_STATUS_OK ||
                    status == GWIPC_STATUS_WOULD_BLOCK,
                gwipc_status_string(status));
      }
    }
    if (until_established &&
        gwipc_connection_get_state(first) == GWIPC_CONNECTION_ESTABLISHED &&
        gwipc_connection_get_state(second) == GWIPC_CONNECTION_ESTABLISHED)
      return;
    if (!until_established) return;
  }
  fail("handshake timed out");
}

void enqueue(gwipc_connection* connection, std::uint16_t type,
             std::uint32_t flags, const std::vector<std::uint8_t>& payload,
             const int* fds = nullptr, std::size_t fd_count = 0) {
  gwipc_outgoing_message outgoing{};
  outgoing.struct_size = sizeof(outgoing);
  outgoing.type = type;
  outgoing.flags = flags;
  outgoing.payload = payload.data();
  outgoing.payload_size = payload.size();
  outgoing.fds = fds;
  outgoing.fd_count = fd_count;
  const auto status = gwipc_connection_enqueue(connection, &outgoing);
  if (status != GWIPC_STATUS_OK)
    std::fprintf(stderr, "enqueue type=0x%04x status=%s\n", type,
                 gwipc_status_string(status));
  require(status == GWIPC_STATUS_OK, gwipc_status_string(status));
}

gwipc_message* receive_type(gwipc_connection* connection, std::uint16_t type) {
  gwipc_message* message = nullptr;
  const auto status = gwipc_connection_receive(connection, &message);
  require(status == GWIPC_STATUS_OK, "expected received message");
  require(gwipc_message_type(message) == type, "unexpected message type");
  return message;
}

}  // namespace

int main() {
  char temporary[] = "/tmp/gwipc-test-XXXXXX";
  require(::mkdtemp(temporary) != nullptr, "mkdtemp failed");
  const std::string directory = temporary;
  const std::string path = directory + "/endpoint.sock";

  gwipc_listener_options listener_options{};
  listener_options.struct_size = sizeof(listener_options);
  listener_options.path = path.c_str();
  listener_options.local_role = GWIPC_ROLE_TEST_CONSUMER;
  listener_options.accepted_peer_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_TEST_PRODUCER);
  listener_options.offered_capabilities = kCapabilities;
  listener_options.required_peer_capabilities = GWIPC_CAP_SNAPSHOTS;
  listener_options.instance_label = "transport-test-server";
  gwipc_listener* listener = nullptr;
  const auto listener_status =
      gwipc_listener_create(&listener_options, &listener);
  if (listener_status != GWIPC_STATUS_OK) {
    std::fprintf(stderr, "listener status=%s errno=%d\n",
                 gwipc_status_string(listener_status), errno);
    fail("listener creation failed");
  }

  struct stat endpoint_status {};
  require(::lstat(path.c_str(), &endpoint_status) == 0 &&
              S_ISSOCK(endpoint_status.st_mode) &&
              (endpoint_status.st_mode & 0777) == 0600,
          "endpoint mode is not 0600");

  gwipc_connection_options connection_options{};
  connection_options.struct_size = sizeof(connection_options);
  connection_options.path = path.c_str();
  connection_options.local_role = GWIPC_ROLE_TEST_PRODUCER;
  connection_options.acceptable_server_roles =
      GWIPC_ROLE_BIT(GWIPC_ROLE_TEST_CONSUMER);
  connection_options.offered_capabilities = kCapabilities;
  connection_options.required_peer_capabilities = GWIPC_CAP_SNAPSHOTS;
  connection_options.instance_label = "transport-test-client";
  gwipc_connection* client = nullptr;
  const auto connect_status =
      gwipc_connection_connect(&connection_options, &client);
  require(connect_status == GWIPC_STATUS_OK ||
              connect_status == GWIPC_STATUS_IN_PROGRESS,
          "connection failed");

  gwipc_connection* server = nullptr;
  for (int attempt = 0; attempt < 100 && !server; ++attempt) {
    const auto status = gwipc_listener_accept(listener, &server);
    if (status == GWIPC_STATUS_WOULD_BLOCK) {
      pollfd descriptor{gwipc_listener_fd(listener), POLLIN, 0};
      (void)::poll(&descriptor, 1, 20);
      continue;
    }
    require(status == GWIPC_STATUS_OK, "accept failed");
  }
  require(server != nullptr, "accept timed out");
  drive(client, server, true);
  require(gwipc_connection_peer_info(client).uid == ::geteuid(),
          "client did not expose peer credentials");
  require(gwipc_connection_peer_info(server).uid == ::geteuid(),
          "server did not expose peer credentials");

  const std::uint64_t nonce = UINT64_C(0x8877665544332211);
  enqueue(client, GWIPC_MESSAGE_PING, GWIPC_FLAG_ACK_REQUIRED,
          gw::ipc::wire::encode(gw::ipc::wire::Ping{nonce}));
  for (int attempt = 0; attempt < 20; ++attempt) drive(client, server);
  auto* pong_message = receive_type(client, GWIPC_MESSAGE_PONG);
  std::size_t pong_size = 0;
  const auto* pong_bytes = gwipc_message_payload(pong_message, &pong_size);
  gw::ipc::wire::Pong pong;
  require(gw::ipc::wire::decode({pong_bytes, pong_size}, pong) ==
                  gw::ipc::wire::CodecStatus::Ok &&
              pong.nonce == nonce &&
              (gwipc_message_flags(pong_message) & GWIPC_FLAG_REPLY) != 0,
          "pong did not correlate");
  gwipc_message_destroy(pong_message);

  const auto begin = gw::ipc::wire::encode(gw::ipc::wire::SnapshotBegin{
      77, gw::ipc::wire::SnapshotDomain::Test, 0, 9, 1});
  enqueue(client, GWIPC_MESSAGE_SNAPSHOT_BEGIN, 0, begin);
  const auto output =
      gw::ipc::wire::encode(gw::ipc::wire::OutputRemove{42});
  enqueue(client, GWIPC_MESSAGE_OUTPUT_REMOVE, GWIPC_FLAG_SNAPSHOT_ITEM,
          output);
  const auto end =
      gw::ipc::wire::encode(gw::ipc::wire::SnapshotEnd{77, 9, 1});
  enqueue(client, GWIPC_MESSAGE_SNAPSHOT_END, 0, end);
  for (int attempt = 0; attempt < 20; ++attempt) drive(client, server);
  for (const auto type : {GWIPC_MESSAGE_SNAPSHOT_BEGIN,
                          GWIPC_MESSAGE_OUTPUT_REMOVE,
                          GWIPC_MESSAGE_SNAPSHOT_END}) {
    auto* message = receive_type(server, type);
    gwipc_message_destroy(message);
  }

  const int memfd = ::memfd_create("gwipc-test", MFD_CLOEXEC | MFD_ALLOW_SEALING);
  require(memfd >= 0 && ::ftruncate(memfd, 4096) == 0, "memfd setup failed");
  gw::ipc::wire::BufferAttach attach;
  attach.buffer_id = 3;
  attach.surface_id = 4;
  attach.width = 8;
  attach.height = 8;
  attach.stride = 32;
  attach.storage_size = 4096;
  enqueue(client, GWIPC_MESSAGE_BUFFER_ATTACH, 0,
          gw::ipc::wire::encode(attach), &memfd, 1);
  (void)::close(memfd);
  for (int attempt = 0; attempt < 20; ++attempt) drive(client, server);
  auto* attach_message = receive_type(server, GWIPC_MESSAGE_BUFFER_ATTACH);
  int received_fd = -1;
  require(gwipc_message_fd_count(attach_message) == 1 &&
              gwipc_message_take_fd(attach_message, 0, &received_fd) ==
                  GWIPC_STATUS_OK &&
              (::fcntl(received_fd, F_GETFD) & FD_CLOEXEC) != 0,
          "received descriptor ownership is invalid");
  (void)::close(received_fd);
  gwipc_message_destroy(attach_message);

  gwipc_connection_destroy(client);
  gwipc_connection_destroy(server);
  (void)::unlink(path.c_str());
  const int replacement = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
  require(replacement >= 0, "replacement file creation failed");
  (void)::close(replacement);
  gwipc_listener_destroy(listener);
  require(::lstat(path.c_str(), &endpoint_status) == 0 &&
              S_ISREG(endpoint_status.st_mode),
          "listener removed a replacement path");
  (void)::unlink(path.c_str());
  (void)::rmdir(directory.c_str());
  return 0;
}
