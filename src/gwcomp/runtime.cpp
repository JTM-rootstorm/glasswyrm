#include "gwcomp/contract_dispatch.hpp"
#include "gwcomp/runtime.hpp"

#include <glasswyrm/ipc.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fcntl.h>
#include <memory>
#include <poll.h>
#include <string>
#include <system_error>
#include <unistd.h>

namespace glasswyrm::compositor {
namespace {

constexpr std::uint64_t kM4Capabilities =
    GWIPC_CAP_FD_PASSING | GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE |
    GWIPC_CAP_SURFACE_STATE | GWIPC_CAP_MEMFD_BUFFERS |
    GWIPC_CAP_DAMAGE_REGIONS | GWIPC_CAP_SDR_COLOR_METADATA |
    GWIPC_CAP_FRAME_ACKNOWLEDGEMENT;
constexpr std::uint64_t kCommonCapabilities =
    GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE | GWIPC_CAP_SURFACE_STATE |
    GWIPC_CAP_SDR_COLOR_METADATA | GWIPC_CAP_FRAME_ACKNOWLEDGEMENT;
constexpr std::uint64_t kOfferedCapabilities =
    kM4Capabilities | GWIPC_CAP_WINDOW_LIFECYCLE;
constexpr std::size_t kMaximumMessagesPerTurn = 64;
constexpr std::size_t kMaximumPayloadBytesPerTurn = 512U * 1024U;
int signal_write_fd = -1;

void wake_for_signal(int) {
  const std::uint8_t byte = 1;
  if (signal_write_fd >= 0) (void)::write(signal_write_fd, &byte, sizeof(byte));
}

struct ListenerDeleter {
  void operator()(gwipc_listener* value) const { gwipc_listener_destroy(value); }
};
struct ConnectionDeleter {
  void operator()(gwipc_connection* value) const {
    gwipc_connection_destroy(value);
  }
};
struct MessageDeleter {
  void operator()(gwipc_message* value) const { gwipc_message_destroy(value); }
};
bool prepare_dump_directory(const std::string& path, std::string& error) {
  std::error_code status_error;
  const auto status = std::filesystem::symlink_status(path, status_error);
  if (!status_error && std::filesystem::is_symlink(status)) {
    error = "dump path must not be a symbolic link";
    return false;
  }
  if (!status_error && std::filesystem::exists(status)) {
    if (!std::filesystem::is_directory(status)) {
      error = "dump path exists but is not a directory";
      return false;
    }
    return true;
  }
  status_error.clear();
  if (!std::filesystem::create_directories(path, status_error) && status_error) {
    error = status_error.message();
    return false;
  }
  const auto created = std::filesystem::symlink_status(path, status_error);
  if (status_error || !std::filesystem::is_directory(created) ||
      std::filesystem::is_symlink(created)) {
    error = "dump path did not resolve to a directory";
    return false;
  }
  return true;
}



}  // namespace

int run(const Options& options) {
  std::string directory_error;
  if (!prepare_dump_directory(options.dump_dir, directory_error)) {
    std::fprintf(stderr, "gwcomp: cannot prepare dump directory: %s\n",
                 directory_error.c_str());
    return 1;
  }

  int signal_pipe[2] = {-1, -1};
  if (::pipe2(signal_pipe, O_NONBLOCK | O_CLOEXEC) != 0) {
    std::perror("gwcomp: signal pipe");
    return 1;
  }
  signal_write_fd = signal_pipe[1];
  const auto previous_int = std::signal(SIGINT, wake_for_signal);
  const auto previous_term = std::signal(SIGTERM, wake_for_signal);

  gwipc_listener_options listener_options{};
  listener_options.struct_size = sizeof(listener_options);
  listener_options.path = options.ipc_socket.c_str();
  listener_options.local_role = GWIPC_ROLE_COMPOSITOR;
  listener_options.accepted_peer_roles =
      GWIPC_ROLE_BIT(GWIPC_ROLE_TEST_PRODUCER) |
      GWIPC_ROLE_BIT(GWIPC_ROLE_PROTOCOL_SERVER);
  listener_options.offered_capabilities = kOfferedCapabilities;
  listener_options.required_peer_capabilities = kCommonCapabilities;
  listener_options.maximum_payload = GWIPC_DEFAULT_MAXIMUM_PAYLOAD;
  listener_options.maximum_fd_count = GWIPC_DEFAULT_MAXIMUM_FDS;
  listener_options.maximum_queued_bytes = GWIPC_DEFAULT_MAXIMUM_QUEUED_BYTES;
  listener_options.maximum_queued_messages = GWIPC_DEFAULT_MAXIMUM_QUEUED_MESSAGES;
  listener_options.instance_label = "gwcomp-m4";
  gwipc_listener* raw_listener = nullptr;
  const auto status = gwipc_listener_create(&listener_options, &raw_listener);
  std::unique_ptr<gwipc_listener, ListenerDeleter> listener(raw_listener);
  if (status != GWIPC_STATUS_OK) {
    std::fprintf(stderr, "gwcomp: listener creation failed: %s errno=%d\n",
                 gwipc_status_string(status),
                 raw_listener ? gwipc_listener_system_errno(raw_listener) : 0);
    signal_write_fd = -1;
    (void)::close(signal_pipe[0]);
    (void)::close(signal_pipe[1]);
    return 1;
  }
  std::fprintf(stderr, "gwcomp: listening socket=%s\n", options.ipc_socket.c_str());

  std::unique_ptr<gwipc_connection, ConnectionDeleter> producer;
  std::optional<std::filesystem::path> manifest_path;
  if (options.scene_manifest) manifest_path = *options.scene_manifest;
  gw::compositor::Compositor compositor(options.dump_dir, manifest_path);
  bool peer_validated = false;
  gwipc_role peer_role = GWIPC_ROLE_UNKNOWN;
  std::optional<gw::compositor::PeerProfile> peer_profile;
  bool accepted_any_frame = false;
  bool stop_after_flush = false;
  bool stopping = false;
  while (!stopping) {
    pollfd descriptors[3] = {
        {gwipc_listener_fd(listener.get()),
         static_cast<short>(producer ? 0 : POLLIN), 0},
        {producer ? gwipc_connection_fd(producer.get()) : -1,
         static_cast<short>(producer
                                ? gwipc_connection_wanted_poll_events(
                                      producer.get())
                                : 0),
         0},
        {signal_pipe[0], POLLIN, 0}};
    const int count = ::poll(descriptors, 3, -1);
    if (count < 0) {
      if (errno == EINTR) continue;
      std::perror("gwcomp: poll");
      break;
    }
    if ((descriptors[2].revents & POLLIN) != 0) {
      std::uint8_t bytes[32];
      while (::read(signal_pipe[0], bytes, sizeof(bytes)) > 0) {}
      stopping = true;
    }
    if (!producer && (descriptors[0].revents & POLLIN) != 0) {
      gwipc_connection* accepted = nullptr;
      if (gwipc_listener_accept(listener.get(), &accepted) == GWIPC_STATUS_OK) {
        producer.reset(accepted);
        const auto peer = gwipc_connection_peer_info(producer.get());
        std::fprintf(stderr, "gwcomp: producer connected pid=%d uid=%u\n",
                     peer.pid, peer.uid);
        peer_validated = false;
        peer_role = GWIPC_ROLE_UNKNOWN;
        peer_profile.reset();
      }
    }
    if (producer && descriptors[1].revents != 0)
      (void)gwipc_connection_process_poll_events(producer.get(),
                                                 descriptors[1].revents);
    if (producer && !peer_validated &&
        gwipc_connection_get_state(producer.get()) ==
            GWIPC_CONNECTION_ESTABLISHED) {
      const auto info = gwipc_connection_peer_info(producer.get());
      peer_role = info.role;
      peer_profile = gw::compositor::select_peer_profile(
          peer_role, info.capabilities);
      if (!peer_profile) {
        std::fprintf(stderr,
                     "gwcomp: peer role=%u has invalid role capability profile\n",
                     static_cast<unsigned>(peer_role));
        compositor.disconnect();
        producer.reset();
        peer_role = GWIPC_ROLE_UNKNOWN;
      } else {
        compositor.set_peer_profile(*peer_profile);
        peer_validated = true;
      }
    }
    std::size_t messages = 0;
    std::size_t payload_bytes = 0;
    while (producer && peer_validated && messages < kMaximumMessagesPerTurn &&
           payload_bytes < kMaximumPayloadBytesPerTurn) {
      gwipc_message* raw_message = nullptr;
      if (gwipc_connection_receive(producer.get(), &raw_message) !=
          GWIPC_STATUS_OK)
        break;
      std::unique_ptr<gwipc_message, MessageDeleter> message(raw_message);
      std::size_t size = 0;
      (void)gwipc_message_payload(message.get(), &size);
      payload_bytes += size;
      ++messages;
      const auto dispatch = dispatch_contract_message(
          producer.get(), message.get(), peer_role, *peer_profile,
          options.max_frames, compositor);
      accepted_any_frame = accepted_any_frame || dispatch.accepted_frame;
      stop_after_flush = stop_after_flush || dispatch.stop_after_flush;
    }
    if (producer && gwipc_connection_get_state(producer.get()) ==
                        GWIPC_CONNECTION_CLOSED) {
      std::fprintf(stderr, "gwcomp: producer disconnected, cleared surfaces=0 buffers=0\n");
      compositor.disconnect();
      producer.reset();
      peer_validated = false;
      peer_role = GWIPC_ROLE_UNKNOWN;
      peer_profile.reset();
      if ((options.once && accepted_any_frame) || stop_after_flush) stopping = true;
    }
  }

  producer.reset();
  listener.reset();
  signal_write_fd = -1;
  (void)::signal(SIGINT, previous_int);
  (void)::signal(SIGTERM, previous_term);
  (void)::close(signal_pipe[0]);
  (void)::close(signal_pipe[1]);
  std::fprintf(stderr, "gwcomp: stopped\n");
  return 0;
}

}  // namespace glasswyrm::compositor
