#include <glasswyrm/ipc.h>

#include "ipc/wire/compositor_contract.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <array>
#include <string>
#include <vector>

namespace {
volatile sig_atomic_t running = 1;
void stop(int) { running = 0; }

constexpr auto kCapabilities =
    GWIPC_CAP_FD_PASSING | GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE |
    GWIPC_CAP_SURFACE_STATE | GWIPC_CAP_MEMFD_BUFFERS |
    GWIPC_CAP_DAMAGE_REGIONS | GWIPC_CAP_SCALE_METADATA |
    GWIPC_CAP_SDR_COLOR_METADATA | GWIPC_CAP_FRAME_ACKNOWLEDGEMENT;

void enqueue_reply(gwipc_connection* connection, const gwipc_message* request,
                   std::uint16_t type,
                   const std::vector<std::uint8_t>& payload) {
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.flags = GWIPC_FLAG_REPLY;
  message.reply_to = gwipc_message_sequence(request);
  message.payload = payload.data();
  message.payload_size = payload.size();
  if (gwipc_connection_enqueue(connection, &message) != GWIPC_STATUS_OK)
    std::fprintf(stderr, "gwipc probe server: reply enqueue failed\n");
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3 || std::strcmp(argv[1], "--socket") != 0) {
    std::fprintf(stderr, "usage: gwipc_probe_server --socket PATH\n");
    return 2;
  }
  (void)::signal(SIGTERM, stop);
  (void)::signal(SIGINT, stop);
  gwipc_listener_options options{};
  options.struct_size = sizeof(options);
  options.path = argv[2];
  options.local_role = GWIPC_ROLE_TEST_CONSUMER;
  options.accepted_peer_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_TEST_PRODUCER);
  options.offered_capabilities = kCapabilities;
  options.maximum_payload = 65536;
  options.maximum_queued_bytes = 65536;
  options.maximum_queued_messages = 64;
  options.instance_label = "gwipc-m3-probe";
  gwipc_listener* raw_listener = nullptr;
  const auto created = gwipc_listener_create(&options, &raw_listener);
  if (created != GWIPC_STATUS_OK) {
    std::fprintf(stderr, "gwipc probe server: create: %s\n",
                 gwipc_status_string(created));
    return 1;
  }
  std::unique_ptr<gwipc_listener, decltype(&gwipc_listener_destroy)> listener(
      raw_listener, gwipc_listener_destroy);
  std::vector<gwipc_connection*> connections;
  std::printf("gwipc probe server: ready socket=%s\n", argv[2]);
  std::fflush(stdout);

  while (running) {
    std::vector<pollfd> pollfds;
    pollfds.push_back({gwipc_listener_fd(listener.get()), POLLIN, 0});
    for (const auto* connection : connections)
      pollfds.push_back({gwipc_connection_fd(connection),
                         gwipc_connection_wanted_poll_events(connection), 0});
    const int result = ::poll(pollfds.data(), pollfds.size(), 100);
    if (result < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if ((pollfds[0].revents & POLLIN) != 0) {
      for (;;) {
        gwipc_connection* connection = nullptr;
        const auto status = gwipc_listener_accept(listener.get(), &connection);
        if (status == GWIPC_STATUS_WOULD_BLOCK) break;
        if (status != GWIPC_STATUS_OK) break;
        connections.push_back(connection);
      }
    }
    for (std::size_t index = 0; index < connections.size(); ++index) {
      auto* connection = connections[index];
      const short revents = index + 1 < pollfds.size()
                                ? pollfds[index + 1].revents
                                : 0;
      if (revents != 0)
        (void)gwipc_connection_process_poll_events(connection, revents);
      for (;;) {
        gwipc_message* message = nullptr;
        if (gwipc_connection_receive(connection, &message) != GWIPC_STATUS_OK)
          break;
        const auto type = gwipc_message_type(message);
        if (type == GWIPC_MESSAGE_BUFFER_ATTACH) {
          int fd = -1;
          bool valid = gwipc_message_take_fd(message, 0, &fd) == GWIPC_STATUS_OK;
          std::array<std::uint8_t, 16> data{};
          if (valid)
            valid = ::pread(fd, data.data(), data.size(), 0) ==
                    static_cast<ssize_t>(data.size());
          for (std::size_t byte = 0; valid && byte < data.size(); ++byte)
            valid = data[byte] == static_cast<std::uint8_t>(byte);
          const int seals = valid ? ::fcntl(fd, F_GET_SEALS) : -1;
          valid = valid && (seals & F_SEAL_SHRINK) != 0 &&
                  (seals & F_SEAL_GROW) != 0 &&
                  (::fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0;
          if (fd >= 0) (void)::close(fd);
          if (valid) {
            gw::ipc::wire::BufferRelease release;
            release.buffer_id = 3;
            enqueue_reply(connection, message, GWIPC_MESSAGE_BUFFER_RELEASE,
                          gw::ipc::wire::encode(release));
          }
        } else if (type == GWIPC_MESSAGE_FRAME_COMMIT) {
          std::size_t size = 0;
          const auto* data = gwipc_message_payload(message, &size);
          gw::ipc::wire::FrameCommit commit;
          if (gw::ipc::wire::decode({data, size}, commit) ==
              gw::ipc::wire::CodecStatus::Ok) {
            gw::ipc::wire::FrameAcknowledged acknowledged;
            acknowledged.commit_id = commit.commit_id;
            acknowledged.output_id = commit.output_id;
            acknowledged.presented_generation = commit.producer_generation;
            enqueue_reply(connection, message,
                          GWIPC_MESSAGE_FRAME_ACKNOWLEDGED,
                          gw::ipc::wire::encode(acknowledged));
          }
        } else if ((gwipc_message_flags(message) & GWIPC_FLAG_ACK_REQUIRED) !=
                   0) {
          std::size_t size = 0;
          const auto* data = gwipc_message_payload(message, &size);
          enqueue_reply(connection, message, type, {data, data + size});
        }
        gwipc_message_destroy(message);
      }
    }
    for (auto iterator = connections.begin(); iterator != connections.end();) {
      if (gwipc_connection_get_state(*iterator) == GWIPC_CONNECTION_CLOSED) {
        gwipc_connection_destroy(*iterator);
        iterator = connections.erase(iterator);
      } else {
        ++iterator;
      }
    }
  }
  for (auto* connection : connections) gwipc_connection_destroy(connection);
  return 0;
}
