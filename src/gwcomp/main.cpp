#include "gwcomp/options.hpp"
#include "gwcomp/compositor.hpp"

#include <glasswyrm/ipc.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <poll.h>
#include <string>
#include <system_error>
#include <unistd.h>

namespace {

constexpr std::uint64_t kRequiredCapabilities =
    GWIPC_CAP_FD_PASSING | GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE |
    GWIPC_CAP_SURFACE_STATE | GWIPC_CAP_MEMFD_BUFFERS |
    GWIPC_CAP_DAMAGE_REGIONS | GWIPC_CAP_SDR_COLOR_METADATA |
    GWIPC_CAP_FRAME_ACKNOWLEDGEMENT;
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
struct ContractDeleter {
  void operator()(gwipc_decoded_contract* value) const {
    gwipc_decoded_contract_destroy(value);
  }
};
struct PayloadDeleter {
  void operator()(gwipc_contract_payload* value) const {
    gwipc_contract_payload_destroy(value);
  }
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

bool is_complete_session_snapshot(const gwipc_message* message) {
  gwipc_decoded_control* raw = nullptr;
  if (gwipc_control_decode_message(message, &raw) != GWIPC_STATUS_OK)
    return false;
  std::unique_ptr<gwipc_decoded_control, decltype(&gwipc_decoded_control_destroy)>
      control(raw, gwipc_decoded_control_destroy);
  const auto* begin = gwipc_decoded_snapshot_begin(control.get());
  return begin && begin->domain == GWIPC_SNAPSHOT_COMPLETE_SESSION;
}

bool enqueue_ack(gwipc_connection* connection, const gwipc_message* message,
                 const gwipc_frame_commit& commit,
                 const gw::compositor::PresentedFrame& frame) {
  gwipc_frame_acknowledged acknowledged{};
  acknowledged.struct_size = sizeof(acknowledged);
  acknowledged.commit_id = commit.commit_id;
  acknowledged.output_id = commit.output_id;
  acknowledged.presented_generation = frame.generation;
  acknowledged.result = frame.result;
  gwipc_contract_payload* raw_payload = nullptr;
  if (gwipc_contract_encode_frame_acknowledged(&acknowledged, &raw_payload) !=
      GWIPC_STATUS_OK)
    return false;
  std::unique_ptr<gwipc_contract_payload, PayloadDeleter> payload(raw_payload);
  std::size_t size = 0;
  const auto* bytes = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message outgoing{};
  outgoing.struct_size = sizeof(outgoing);
  outgoing.type = GWIPC_MESSAGE_FRAME_ACKNOWLEDGED;
  outgoing.flags = GWIPC_FLAG_REPLY;
  outgoing.reply_to = gwipc_message_sequence(message);
  outgoing.payload = bytes;
  outgoing.payload_size = size;
  const auto status = gwipc_connection_enqueue(connection, &outgoing);
  if (status != GWIPC_STATUS_OK)
    std::fprintf(stderr, "gwcomp: acknowledgement enqueue failed: %s\n",
                 gwipc_status_string(status));
  return status == GWIPC_STATUS_OK;
}

bool enqueue_release(gwipc_connection* connection, std::uint64_t buffer_id,
                     gwipc_buffer_release_reason reason) {
  gwipc_buffer_release release{};
  release.struct_size = sizeof(release);
  release.buffer_id = buffer_id;
  release.reason = reason;
  gwipc_contract_payload* raw_payload = nullptr;
  if (gwipc_contract_encode_buffer_release(&release, &raw_payload) != GWIPC_STATUS_OK)
    return false;
  std::unique_ptr<gwipc_contract_payload, PayloadDeleter> payload(raw_payload);
  std::size_t size = 0;
  const auto* bytes = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message outgoing{};
  outgoing.struct_size = sizeof(outgoing);
  outgoing.type = GWIPC_MESSAGE_BUFFER_RELEASE;
  outgoing.payload = bytes;
  outgoing.payload_size = size;
  return gwipc_connection_enqueue(connection, &outgoing) == GWIPC_STATUS_OK;
}

int run(const glasswyrm::compositor::Options& options) {
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
  listener_options.accepted_peer_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_TEST_PRODUCER);
  listener_options.offered_capabilities = kRequiredCapabilities;
  listener_options.required_peer_capabilities = kRequiredCapabilities;
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
  gw::compositor::Compositor compositor(options.dump_dir);
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
      }
    }
    if (producer && descriptors[1].revents != 0)
      (void)gwipc_connection_process_poll_events(producer.get(),
                                                 descriptors[1].revents);
    std::size_t messages = 0;
    std::size_t payload_bytes = 0;
    while (producer && messages < kMaximumMessagesPerTurn &&
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
      const auto type = gwipc_message_type(message.get());
      if (type == GWIPC_MESSAGE_SNAPSHOT_BEGIN) {
        if (!is_complete_session_snapshot(message.get()) || !compositor.begin_snapshot())
          std::fprintf(stderr, "gwcomp: rejected invalid snapshot begin\n");
        continue;
      }
      if (type == GWIPC_MESSAGE_SNAPSHOT_END) {
        if (!compositor.end_snapshot())
          std::fprintf(stderr, "gwcomp: rejected invalid snapshot end\n");
        continue;
      }
      if (type == GWIPC_MESSAGE_SNAPSHOT_ABORT) {
        compositor.abort_snapshot();
        continue;
      }
      gwipc_decoded_contract* raw_contract = nullptr;
      if (gwipc_contract_decode_message(message.get(), &raw_contract) != GWIPC_STATUS_OK) {
        std::fprintf(stderr, "gwcomp: rejected undecodable contract type=0x%04x\n", type);
        continue;
      }
      std::unique_ptr<gwipc_decoded_contract, ContractDeleter> contract(raw_contract);
      bool applied = true;
      switch (type) {
        case GWIPC_MESSAGE_OUTPUT_UPSERT:
          applied = compositor.apply(*gwipc_decoded_output_upsert(contract.get())); break;
        case GWIPC_MESSAGE_OUTPUT_REMOVE:
          applied = compositor.apply(*gwipc_decoded_output_remove(contract.get())); break;
        case GWIPC_MESSAGE_SURFACE_UPSERT:
          applied = compositor.apply(*gwipc_decoded_surface_upsert(contract.get())); break;
        case GWIPC_MESSAGE_SURFACE_REMOVE:
          applied = compositor.apply(*gwipc_decoded_surface_remove(contract.get())); break;
        case GWIPC_MESSAGE_SURFACE_DAMAGE:
          applied = compositor.apply(*gwipc_decoded_surface_damage(contract.get())); break;
        case GWIPC_MESSAGE_BUFFER_ATTACH: {
          int fd = -1;
          std::string error;
          applied = gwipc_message_take_fd(message.get(), 0, &fd) == GWIPC_STATUS_OK &&
                    compositor.attach(*gwipc_decoded_buffer_attach(contract.get()), fd, error);
          if (!applied) std::fprintf(stderr, "gwcomp: buffer rejected: %s\n", error.c_str());
          break;
        }
        case GWIPC_MESSAGE_BUFFER_DETACH:
          applied = compositor.detach(*gwipc_decoded_buffer_detach(contract.get())); break;
        case GWIPC_MESSAGE_FRAME_COMMIT: {
          const auto& commit = *gwipc_decoded_frame_commit(contract.get());
          std::string error;
          auto frame = compositor.commit(commit, error);
          if ((gwipc_message_flags(message.get()) & GWIPC_FLAG_ACK_REQUIRED) == 0)
            frame.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
          (void)enqueue_ack(producer.get(), message.get(), commit, frame);
          if (frame.result == GWIPC_FRAME_ACCEPTED) {
            accepted_any_frame = true;
            std::fprintf(stderr, "gwcomp: frame accepted commit=%llu frame=%llu hash=%016llx\n",
                         static_cast<unsigned long long>(commit.commit_id),
                         static_cast<unsigned long long>(frame.ordinal),
                         static_cast<unsigned long long>(frame.hash));
            if (options.max_frames && compositor.accepted_frames() == *options.max_frames)
              stop_after_flush = true;
          } else {
            std::fprintf(stderr, "gwcomp: frame rejected result=%u reason=%s\n",
                         static_cast<unsigned>(frame.result), error.c_str());
          }
          break;
        }
        default: applied = false; break;
      }
      if (!applied && type != GWIPC_MESSAGE_FRAME_COMMIT)
        std::fprintf(stderr, "gwcomp: rejected contract type=0x%04x\n", type);
      for (const auto& [buffer_id, reason] : compositor.releases())
        (void)enqueue_release(producer.get(), buffer_id, reason);
      compositor.clear_releases();
    }
    if (producer && gwipc_connection_get_state(producer.get()) ==
                        GWIPC_CONNECTION_CLOSED) {
      std::fprintf(stderr, "gwcomp: producer disconnected, cleared surfaces=0 buffers=0\n");
      compositor.disconnect();
      producer.reset();
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

}  // namespace

int main(int argc, char** argv) {
  glasswyrm::compositor::Options options;
  const auto result = glasswyrm::compositor::parse_options(
      argc, argv, options, std::cout, std::cerr);
  if (result == glasswyrm::compositor::ParseOptionsResult::ExitSuccess) return 0;
  if (result == glasswyrm::compositor::ParseOptionsResult::ExitFailure) return 2;
  return run(options);
}
