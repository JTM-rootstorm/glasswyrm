#include "gwcomp/contract_dispatch.hpp"
#include "gwcomp/runtime.hpp"
#include "gwcomp/signal_runtime.hpp"

#include "backends/output/presentation_backend.hpp"
#include "config.hpp"
#if GW_HAS_HEADLESS_BACKEND
#include "backends/headless/presenter.hpp"
#endif

#include <glasswyrm/ipc.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <filesystem>
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
  if (options.backend == Backend::Headless &&
      !prepare_dump_directory(options.dump_dir, directory_error)) {
    std::fprintf(stderr, "gwcomp: cannot prepare dump directory: %s\n",
                 directory_error.c_str());
    return 1;
  }

  SignalRuntime signals;
  std::string signal_error;
  if (!signals.install(options.backend == Backend::Drm &&
                           !options.external_session,
                       signal_error)) {
    std::fprintf(stderr, "gwcomp: %s\n", signal_error.c_str());
    return 1;
  }

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
    return 1;
  }
  std::fprintf(stderr, "gwcomp: listening socket=%s\n", options.ipc_socket.c_str());

  std::unique_ptr<gwipc_connection, ConnectionDeleter> producer;
  std::optional<std::filesystem::path> manifest_path;
  if (options.scene_manifest) manifest_path = *options.scene_manifest;
  std::unique_ptr<glasswyrm::output::PresentationBackend> presenter;
  if (options.backend == Backend::Headless) {
#if GW_HAS_HEADLESS_BACKEND
    presenter = std::make_unique<glasswyrm::headless::Presenter>(
        options.dump_dir);
#else
    std::fprintf(stderr,
                 "gwcomp: headless backend was not enabled at build time\n");
    return 1;
#endif
  } else {
    std::fprintf(stderr,
                 "gwcomp: DRM backend initialization is not yet available\n");
    return 1;
  }
  gw::compositor::Compositor compositor(std::move(presenter), manifest_path);
  bool peer_validated = false;
  gwipc_role peer_role = GWIPC_ROLE_UNKNOWN;
  std::optional<gw::compositor::PeerProfile> peer_profile;
  bool accepted_any_frame = false;
  bool stop_after_flush = false;
  bool stopping = false;
  bool vt_release_requested = false;
  bool vt_acquire_requested = false;
  int exit_status = 0;
  std::optional<std::uint64_t> pending_reply_sequence;
  while (!stopping) {
    pollfd descriptors[4] = {
        {gwipc_listener_fd(listener.get()),
         static_cast<short>(producer ? 0 : POLLIN), 0},
        {producer ? gwipc_connection_fd(producer.get()) : -1,
         static_cast<short>(producer
                                ? gwipc_connection_wanted_poll_events(
                                      producer.get())
                                : 0),
         0},
        {signals.poll_fd(), POLLIN, 0},
        {compositor.presentation_poll_fd(),
         compositor.presentation_poll_events(), 0}};
    const int count =
        ::poll(descriptors, 4, compositor.presentation_timeout_ms());
    if (count < 0) {
      if (errno == EINTR) continue;
      std::perror("gwcomp: poll");
      exit_status = 1;
      break;
    }
    if ((descriptors[2].revents & POLLIN) != 0) {
      const auto events = signals.drain();
      if (events.stop) stopping = true;
      if (!stopping && events.virtual_terminal_release) {
        vt_release_requested = true;
      }
      if (!stopping && events.virtual_terminal_acquire) {
        vt_acquire_requested = true;
      }
    }
    if (stopping) continue;
    if (compositor.presentation_pending() &&
        (descriptors[3].revents != 0 ||
         compositor.presentation_timeout_ms() == 0)) {
      std::string presentation_error;
      const auto completion = compositor.service_presentation(
          descriptors[3].revents, presentation_error);
      if (completion.kind ==
          gw::compositor::PresentationCompletionKind::Fatal) {
        std::fprintf(stderr, "gwcomp: presentation failed: %s\n",
                     presentation_error.c_str());
        stopping = true;
        exit_status = 1;
      } else if (completion.kind ==
                 gw::compositor::PresentationCompletionKind::Complete) {
        if (!producer || !pending_reply_sequence) {
          std::fprintf(stderr,
                       "gwcomp: presentation completed without a producer reply target\n");
          stopping = true;
          exit_status = 1;
        } else {
          const auto dispatch = complete_contract_presentation(
              producer.get(), *pending_reply_sequence, completion,
              options.max_frames, compositor);
          pending_reply_sequence.reset();
          accepted_any_frame =
              accepted_any_frame || dispatch.accepted_frame;
          stop_after_flush = stop_after_flush || dispatch.stop_after_flush;
        }
      }
    }
    if (stopping) continue;
    if (vt_release_requested && !compositor.presentation_pending() &&
        !compositor.presentation_suspended()) {
      std::string vt_error;
      if (!compositor.suspend_presentation(vt_error)) {
        std::fprintf(stderr, "gwcomp: VT release failed: %s\n",
                     vt_error.c_str());
        stopping = true;
        exit_status = 1;
      } else {
        vt_release_requested = false;
      }
    }
    if (!stopping && vt_acquire_requested &&
        compositor.presentation_suspended()) {
      std::string vt_error;
      if (!compositor.resume_presentation(vt_error)) {
        std::fprintf(stderr, "gwcomp: VT acquire failed: %s\n",
                     vt_error.c_str());
        stopping = true;
        exit_status = 1;
      } else {
        vt_acquire_requested = false;
      }
    }
    if (stopping) continue;
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
    while (producer && peer_validated && !compositor.presentation_pending() &&
           !compositor.presentation_suspended() && !vt_release_requested &&
           messages < kMaximumMessagesPerTurn &&
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
      if (dispatch.pending_frame)
        pending_reply_sequence = dispatch.reply_sequence;
      if (dispatch.fatal) {
        stopping = true;
        exit_status = 1;
        break;
      }
    }
    if (producer && gwipc_connection_get_state(producer.get()) ==
                        GWIPC_CONNECTION_CLOSED) {
      if (compositor.presentation_pending()) {
        std::fprintf(stderr,
                     "gwcomp: producer disconnected during a pending presentation\n");
        stopping = true;
        exit_status = 1;
        continue;
      }
      std::fprintf(stderr, "gwcomp: producer disconnected, cleared surfaces=0 buffers=0\n");
      compositor.disconnect();
      producer.reset();
      peer_validated = false;
      peer_role = GWIPC_ROLE_UNKNOWN;
      peer_profile.reset();
      if ((options.once && accepted_any_frame) || stop_after_flush) stopping = true;
    }
  }

  std::string shutdown_error;
  if (!compositor.shutdown_presentation(shutdown_error)) {
    std::fprintf(stderr, "gwcomp: presentation restore failed: %s\n",
                 shutdown_error.c_str());
    exit_status = 1;
  }
  producer.reset();
  listener.reset();
  std::fprintf(stderr, "gwcomp: stopped\n");
  return exit_status;
}

}  // namespace glasswyrm::compositor
