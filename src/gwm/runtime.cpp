#include "gwm/runtime.hpp"

#include "gwm/contract_dispatch.hpp"
#include "gwm/signal_runtime.hpp"

#include <glasswyrm/ipc.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <poll.h>

namespace glasswyrm::wm::runtime {
namespace {

constexpr std::uint64_t kRequiredCapabilities =
    GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_WINDOW_POLICY;
constexpr std::uint64_t kOfferedCapabilities =
    kRequiredCapabilities | GWIPC_CAP_WINDOW_LIFECYCLE |
    GWIPC_CAP_INTERACTIVE_POLICY;
constexpr std::size_t kMaximumMessagesPerTurn = 64;
constexpr std::size_t kMaximumPayloadBytesPerTurn = 512U * 1024U;
constexpr std::uint32_t kMaximumQueuedBytes = GWIPC_HARD_MAXIMUM_QUEUED_BYTES;
constexpr std::uint16_t kMaximumQueuedMessages = 8192;

struct ListenerDeleter {
  void operator()(gwipc_listener* value) const { gwipc_listener_destroy(value); }
};
struct ConnectionDeleter {
  void operator()(gwipc_connection* value) const { gwipc_connection_destroy(value); }
};
struct MessageDeleter {
  void operator()(gwipc_message* value) const { gwipc_message_destroy(value); }
};

}  // namespace

int run(const glasswyrm::wm::Options& options) {
  SignalRuntime signals;
  if (!install_signal_runtime(signals)) return 1;

  gwipc_listener_options listener_options{};
  listener_options.struct_size = sizeof(listener_options);
  listener_options.path = options.ipc_socket.c_str();
  listener_options.local_role = GWIPC_ROLE_WINDOW_MANAGER;
  listener_options.accepted_peer_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_PROTOCOL_SERVER);
  listener_options.offered_capabilities = kOfferedCapabilities;
  listener_options.required_peer_capabilities = kRequiredCapabilities;
  listener_options.maximum_payload = GWIPC_DEFAULT_MAXIMUM_PAYLOAD;
  listener_options.maximum_fd_count = GWIPC_DEFAULT_MAXIMUM_FDS;
  listener_options.maximum_queued_bytes = kMaximumQueuedBytes;
  listener_options.maximum_queued_messages = kMaximumQueuedMessages;
  listener_options.instance_label = "gwm-m5";
  gwipc_listener* raw_listener = nullptr;
  const auto status = gwipc_listener_create(&listener_options, &raw_listener);
  const std::unique_ptr<gwipc_listener, ListenerDeleter> listener(raw_listener);
  if (status != GWIPC_STATUS_OK) {
    std::fprintf(stderr, "gwm: listener creation failed: %s errno=%d\n",
                 gwipc_status_string(status),
                 raw_listener ? gwipc_listener_system_errno(raw_listener) : 0);
    close_signal_runtime(signals, false);
    return 1;
  }
  std::fprintf(stderr, "gwm: listening socket=%s\n", options.ipc_socket.c_str());

  std::unique_ptr<gwipc_connection, ConnectionDeleter> producer;
  PeerState peer;
  bool accepted_any = false;
  std::uint64_t accepted_commits = 0;
  bool stop_after_flush = false;
  bool flush_complete = false;
  bool stopping = false;
  while (!stopping) {
    pollfd descriptors[3] = {
        {gwipc_listener_fd(listener.get()),
         static_cast<short>(producer ? 0 : POLLIN), 0},
        {producer ? gwipc_connection_fd(producer.get()) : -1,
         static_cast<short>(producer
                                ? gwipc_connection_wanted_poll_events(producer.get())
                                : 0),
         0},
        {signals.read_fd, POLLIN, 0}};
    const int count = ::poll(descriptors, 3, flush_complete ? 1000 : -1);
    if (count < 0) {
      if (errno == EINTR) continue;
      std::perror("gwm: poll");
      break;
    }
    if (count == 0 && flush_complete) {
      stopping = true;
      continue;
    }
    if ((descriptors[2].revents & POLLIN) != 0) {
      drain_signal_runtime(signals);
      stopping = true;
    }
    if (!producer && (descriptors[0].revents & POLLIN) != 0) {
      gwipc_connection* accepted_connection = nullptr;
      if (gwipc_listener_accept(listener.get(), &accepted_connection) ==
          GWIPC_STATUS_OK) {
        producer.reset(accepted_connection);
        const auto info = gwipc_connection_peer_info(producer.get());
        std::fprintf(stderr,
                     "gwm: protocol server connected pid=%d uid=%u\n",
                     info.pid, info.uid);
      }
    }
    if (producer && descriptors[1].revents != 0)
      (void)gwipc_connection_process_poll_events(producer.get(),
                                                 descriptors[1].revents);
    std::size_t messages = 0;
    std::size_t payload_bytes = 0;
    while (producer && !stop_after_flush && messages < kMaximumMessagesPerTurn &&
           payload_bytes < kMaximumPayloadBytesPerTurn) {
      gwipc_message* raw_message = nullptr;
      if (gwipc_connection_receive(producer.get(), &raw_message) !=
          GWIPC_STATUS_OK)
        break;
      const std::unique_ptr<gwipc_message, MessageDeleter> message(raw_message);
      std::size_t size = 0;
      (void)gwipc_message_payload(message.get(), &size);
      payload_bytes += size;
      ++messages;
      const auto type = gwipc_message_type(message.get());
      bool accepted = false;
      const bool handled =
          type == GWIPC_MESSAGE_SNAPSHOT_BEGIN ||
                  type == GWIPC_MESSAGE_SNAPSHOT_END ||
                  type == GWIPC_MESSAGE_SNAPSHOT_ABORT
              ? dispatch_control(peer, message.get())
              : dispatch_contract(peer, producer.get(), message.get(), accepted);
      if (!handled) {
        std::fprintf(stderr, "gwm: rejected message type=0x%04x\n", type);
        if (type == GWIPC_MESSAGE_POLICY_COMMIT) {
          peer.disconnect();
          producer.reset();
        }
      }
      if (accepted) {
        accepted_any = true;
        ++accepted_commits;
        if (options.max_commits &&
            accepted_commits == *options.max_commits)
          stop_after_flush = true;
      }
    }
    if (producer && stop_after_flush &&
        (gwipc_connection_wanted_poll_events(producer.get()) & POLLOUT) == 0)
      flush_complete = true;
    if (producer && gwipc_connection_get_state(producer.get()) ==
                        GWIPC_CONNECTION_CLOSED) {
      const auto windows = peer.transaction.committed_raw().windows.size();
      std::fprintf(stderr,
                   "gwm: protocol server disconnected, cleared windows=%zu\n",
                   windows);
      peer.disconnect();
      producer.reset();
      if ((options.once && accepted_any) || stop_after_flush) stopping = true;
    }
  }

  producer.reset();
  close_signal_runtime(signals, true);
  std::fprintf(stderr, "gwm: stopped\n");
  return 0;
}


}  // namespace glasswyrm::wm::runtime
