#include "gwm/options.hpp"

#include "wm/transaction.hpp"

#include <glasswyrm/ipc.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <poll.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

constexpr std::uint64_t kRequiredCapabilities =
    GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_WINDOW_POLICY;
constexpr std::size_t kMaximumMessagesPerTurn = 64;
constexpr std::size_t kMaximumPayloadBytesPerTurn = 512U * 1024U;
constexpr std::uint32_t kMaximumQueuedBytes = GWIPC_HARD_MAXIMUM_QUEUED_BYTES;
constexpr std::uint16_t kMaximumQueuedMessages = 8192;
int signal_write_fd = -1;

void wake_for_signal(int) {
  const std::uint8_t byte = 1;
  if (signal_write_fd >= 0) (void)::write(signal_write_fd, &byte, sizeof(byte));
}

struct ListenerDeleter {
  void operator()(gwipc_listener* value) const { gwipc_listener_destroy(value); }
};
struct ConnectionDeleter {
  void operator()(gwipc_connection* value) const { gwipc_connection_destroy(value); }
};
struct MessageDeleter {
  void operator()(gwipc_message* value) const { gwipc_message_destroy(value); }
};
struct ContractDeleter {
  void operator()(gwipc_decoded_contract* value) const {
    gwipc_decoded_contract_destroy(value);
  }
};
struct ControlDeleter {
  void operator()(gwipc_decoded_control* value) const {
    gwipc_decoded_control_destroy(value);
  }
};
struct ContractPayloadDeleter {
  void operator()(gwipc_contract_payload* value) const {
    gwipc_contract_payload_destroy(value);
  }
};
struct ControlPayloadDeleter {
  void operator()(gwipc_control_payload* value) const {
    gwipc_control_payload_destroy(value);
  }
};

glasswyrm::wm::Context context_from(const gwipc_policy_context_upsert& value) {
  return {value.root_window_id, value.workspace_id, value.output_id,
          value.work_x, value.work_y, value.work_width, value.work_height,
          value.flags};
}

glasswyrm::wm::RawWindow window_from(const gwipc_policy_window_upsert& value) {
  using glasswyrm::wm::DecorationPreference;
  using glasswyrm::wm::WindowType;
  return {value.window_id,
          value.parent_window_id,
          value.transient_for,
          value.workspace_id,
          value.requested_x,
          value.requested_y,
          value.requested_width,
          value.requested_height,
          value.border_width,
          static_cast<WindowType>(value.window_type),
          value.map_intent == GWIPC_POLICY_WANTS_MAP,
          value.override_redirect != 0,
          static_cast<DecorationPreference>(value.decoration_preference),
          value.fullscreen_requested != 0,
          value.maximized_requested != 0,
          value.minimized_requested != 0,
          value.attention_requested != 0,
          value.creation_serial,
          value.map_serial,
          value.focus_serial,
          value.flags};
}

gwipc_policy_result result_from(glasswyrm::wm::EvaluationError error) {
  using glasswyrm::wm::EvaluationError;
  switch (error) {
    case EvaluationError::None: return GWIPC_POLICY_ACCEPTED;
    case EvaluationError::IncompleteSnapshot:
      return GWIPC_POLICY_REJECTED_INCOMPLETE_SNAPSHOT;
    case EvaluationError::InvalidContext:
      return GWIPC_POLICY_REJECTED_INVALID_CONTEXT;
    case EvaluationError::InvalidWindow:
      return GWIPC_POLICY_REJECTED_INVALID_WINDOW;
    case EvaluationError::UnknownReference:
      return GWIPC_POLICY_REJECTED_UNKNOWN_REFERENCE;
    case EvaluationError::Limit: return GWIPC_POLICY_REJECTED_LIMIT;
    case EvaluationError::UnsupportedMetadata:
      return GWIPC_POLICY_REJECTED_UNSUPPORTED_METADATA;
    case EvaluationError::OutputFailure: return GWIPC_POLICY_REJECTED_LIMIT;
  }
  return GWIPC_POLICY_REJECTED_INVALID_WINDOW;
}

gwipc_policy_window_state state_from(const glasswyrm::wm::WindowState& value) {
  gwipc_policy_window_state state{};
  state.struct_size = sizeof(state);
  state.window_id = value.window_id;
  state.transient_for = value.transient_for;
  state.workspace_id = value.workspace_id;
  state.output_id = value.output_id;
  state.final_x = value.final_x;
  state.final_y = value.final_y;
  state.final_width = value.final_width;
  state.final_height = value.final_height;
  state.stacking = value.stacking;
  state.window_type = static_cast<gwipc_policy_window_type>(value.window_type);
  state.applied_state =
      static_cast<gwipc_policy_applied_state>(value.applied_state);
  state.visible = value.visible;
  state.focused = value.focused;
  state.managed = value.managed;
  state.decoration_eligible = value.decoration_eligible;
  state.override_redirect = value.override_redirect;
  state.attention_requested = value.attention_requested;
  state.fullscreen_eligible =
      static_cast<gwipc_tri_state>(value.fullscreen_eligible);
  state.direct_scanout_eligible =
      static_cast<gwipc_tri_state>(value.direct_scanout_eligible);
  return state;
}

template <class Value, class Encoder>
bool enqueue_contract(gwipc_connection* connection, std::uint16_t type,
                      std::uint32_t flags, std::uint64_t reply_to,
                      const Value& value, Encoder encoder) {
  gwipc_contract_payload* raw = nullptr;
  if (encoder(&value, &raw) != GWIPC_STATUS_OK) return false;
  const std::unique_ptr<gwipc_contract_payload, ContractPayloadDeleter> payload(raw);
  std::size_t size = 0;
  const auto* bytes = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message outgoing{};
  outgoing.struct_size = sizeof(outgoing);
  outgoing.type = type;
  outgoing.flags = flags;
  outgoing.reply_to = reply_to;
  outgoing.payload = bytes;
  outgoing.payload_size = size;
  return gwipc_connection_enqueue(connection, &outgoing) == GWIPC_STATUS_OK;
}

template <class Value, class Encoder>
bool enqueue_control(gwipc_connection* connection, std::uint16_t type,
                     const Value& value, Encoder encoder) {
  gwipc_control_payload* raw = nullptr;
  if (encoder(&value, &raw) != GWIPC_STATUS_OK) return false;
  const std::unique_ptr<gwipc_control_payload, ControlPayloadDeleter> payload(raw);
  std::size_t size = 0;
  const auto* bytes = gwipc_control_payload_data(payload.get(), &size);
  gwipc_outgoing_message outgoing{};
  outgoing.struct_size = sizeof(outgoing);
  outgoing.type = type;
  outgoing.payload = bytes;
  outgoing.payload_size = size;
  return gwipc_connection_enqueue(connection, &outgoing) == GWIPC_STATUS_OK;
}

bool preflight_policy(const glasswyrm::wm::PolicyState& policy) {
  if (policy.windows.size() > kMaximumQueuedMessages - 3U) return false;
  std::size_t bytes = (policy.windows.size() + 3U) * 40U;
  for (const auto id : policy.output_order) {
    const auto state = state_from(policy.windows.at(id));
    gwipc_contract_payload* raw = nullptr;
    if (gwipc_contract_encode_policy_window_state(&state, &raw) !=
        GWIPC_STATUS_OK)
      return false;
    const std::unique_ptr<gwipc_contract_payload, ContractPayloadDeleter> payload(raw);
    std::size_t size = 0;
    (void)gwipc_contract_payload_data(payload.get(), &size);
    if (size > kMaximumQueuedBytes - bytes) return false;
    bytes += size;
  }
  return bytes <= kMaximumQueuedBytes;
}

bool enqueue_policy(gwipc_connection* connection, const gwipc_message* commit_message,
                    const gwipc_policy_commit& commit,
                    const glasswyrm::wm::PolicyState& policy) {
  const auto count = static_cast<std::uint32_t>(policy.output_order.size());
  gwipc_snapshot_begin begin{};
  begin.struct_size = sizeof(begin);
  begin.snapshot_id = commit.commit_id;
  begin.domain = GWIPC_SNAPSHOT_WINDOW_POLICY;
  begin.generation = policy.generation;
  begin.expected_item_count = count;
  if (!enqueue_control(connection, GWIPC_MESSAGE_SNAPSHOT_BEGIN, begin,
                       gwipc_control_encode_snapshot_begin))
    return false;
  for (const auto id : policy.output_order) {
    const auto state = state_from(policy.windows.at(id));
    if (!enqueue_contract(connection, GWIPC_MESSAGE_POLICY_WINDOW_STATE,
                          GWIPC_FLAG_SNAPSHOT_ITEM, 0, state,
                          gwipc_contract_encode_policy_window_state))
      return false;
  }
  gwipc_snapshot_end end{};
  end.struct_size = sizeof(end);
  end.snapshot_id = begin.snapshot_id;
  end.generation = begin.generation;
  end.actual_item_count = count;
  if (!enqueue_control(connection, GWIPC_MESSAGE_SNAPSHOT_END, end,
                       gwipc_control_encode_snapshot_end))
    return false;
  gwipc_policy_acknowledged ack{};
  ack.struct_size = sizeof(ack);
  ack.commit_id = commit.commit_id;
  ack.producer_generation = commit.producer_generation;
  ack.applied_generation = policy.generation;
  ack.policy_hash = policy.hash;
  ack.window_count = count;
  ack.result = GWIPC_POLICY_ACCEPTED;
  return enqueue_contract(connection, GWIPC_MESSAGE_POLICY_ACKNOWLEDGED,
                          GWIPC_FLAG_REPLY,
                          gwipc_message_sequence(commit_message), ack,
                          gwipc_contract_encode_policy_acknowledged);
}

bool enqueue_rejection(gwipc_connection* connection,
                       const gwipc_message* commit_message,
                       const gwipc_policy_commit& commit,
                       const glasswyrm::wm::PolicyState& previous,
                       gwipc_policy_result result) {
  gwipc_policy_acknowledged ack{};
  ack.struct_size = sizeof(ack);
  ack.commit_id = commit.commit_id;
  ack.producer_generation = commit.producer_generation;
  ack.applied_generation = previous.generation;
  ack.policy_hash = previous.hash;
  ack.window_count = static_cast<std::uint32_t>(previous.windows.size());
  ack.result = result;
  return enqueue_contract(connection, GWIPC_MESSAGE_POLICY_ACKNOWLEDGED,
                          GWIPC_FLAG_REPLY,
                          gwipc_message_sequence(commit_message), ack,
                          gwipc_contract_encode_policy_acknowledged);
}

struct PeerState {
  glasswyrm::wm::Transaction transaction;
  std::uint64_t snapshot_id{};
  std::uint64_t snapshot_generation{};
  std::uint64_t last_commit_id{};
  std::uint64_t last_generation{};
  std::uint64_t accepted_commits{};

  void disconnect() noexcept { *this = {}; }
};

bool dispatch_control(PeerState& peer, const gwipc_message* message) {
  gwipc_decoded_control* raw = nullptr;
  if (gwipc_control_decode_message(message, &raw) != GWIPC_STATUS_OK)
    return false;
  const std::unique_ptr<gwipc_decoded_control, ControlDeleter> control(raw);
  switch (gwipc_message_type(message)) {
    case GWIPC_MESSAGE_SNAPSHOT_BEGIN: {
      const auto* value = gwipc_decoded_snapshot_begin(control.get());
      if (!value || value->domain != GWIPC_SNAPSHOT_WINDOW_POLICY ||
          !peer.transaction.begin_snapshot())
        return false;
      peer.snapshot_id = value->snapshot_id;
      peer.snapshot_generation = value->generation;
      std::fprintf(stderr, "gwm: snapshot begin id=%llu generation=%llu\n",
                   static_cast<unsigned long long>(value->snapshot_id),
                   static_cast<unsigned long long>(value->generation));
      return true;
    }
    case GWIPC_MESSAGE_SNAPSHOT_END: {
      const auto* value = gwipc_decoded_snapshot_end(control.get());
      const bool valid = value && value->snapshot_id == peer.snapshot_id &&
                         value->generation == peer.snapshot_generation &&
                         peer.transaction.end_snapshot();
      if (valid) {
        peer.snapshot_id = 0;
        peer.snapshot_generation = 0;
      }
      return valid;
    }
    case GWIPC_MESSAGE_SNAPSHOT_ABORT: {
      const auto* value = gwipc_decoded_snapshot_abort(control.get());
      const bool valid = value && value->snapshot_id == peer.snapshot_id &&
                         peer.transaction.abort_snapshot();
      if (valid) {
        peer.snapshot_id = 0;
        peer.snapshot_generation = 0;
      }
      return valid;
    }
    default: return false;
  }
}

bool dispatch_contract(PeerState& peer, gwipc_connection* connection,
                       const gwipc_message* message, bool& accepted) {
  gwipc_decoded_contract* raw = nullptr;
  if (gwipc_contract_decode_message(message, &raw) != GWIPC_STATUS_OK)
    return false;
  const std::unique_ptr<gwipc_decoded_contract, ContractDeleter> contract(raw);
  switch (gwipc_message_type(message)) {
    case GWIPC_MESSAGE_POLICY_CONTEXT_UPSERT: {
      const auto* value = gwipc_decoded_policy_context_upsert(contract.get());
      if (!value || !peer.transaction.upsert(context_from(*value))) return false;
      std::fprintf(stderr,
                   "gwm: context workspace=%u output=%llu work_area=%ux%u+%d+%d\n",
                   value->workspace_id,
                   static_cast<unsigned long long>(value->output_id),
                   value->work_width, value->work_height, value->work_x,
                   value->work_y);
      return true;
    }
    case GWIPC_MESSAGE_POLICY_WINDOW_UPSERT: {
      const auto* value = gwipc_decoded_policy_window_upsert(contract.get());
      if (!value || !peer.transaction.upsert(window_from(*value))) return false;
      std::fprintf(stderr, "gwm: window upsert id=%u map=%u override_redirect=%u\n",
                   value->window_id, static_cast<unsigned>(value->map_intent),
                   static_cast<unsigned>(value->override_redirect));
      return true;
    }
    case GWIPC_MESSAGE_POLICY_WINDOW_REMOVE: {
      const auto* value = gwipc_decoded_policy_window_remove(contract.get());
      return value && peer.transaction.remove(value->window_id);
    }
    case GWIPC_MESSAGE_POLICY_COMMIT: {
      const auto* value = gwipc_decoded_policy_commit(contract.get());
      if (!value) return false;
      if (value->commit_id <= peer.last_commit_id ||
          value->producer_generation < peer.last_generation) {
        return enqueue_rejection(connection, message, *value,
                                 peer.transaction.committed_policy(),
                                 GWIPC_POLICY_REJECTED_INVALID_WINDOW);
      }
      peer.last_commit_id = value->commit_id;
      peer.last_generation = value->producer_generation;
      auto evaluation = peer.transaction.commit(value->producer_generation,
                                                 preflight_policy);
      if (!evaluation) {
        if (evaluation.error == glasswyrm::wm::EvaluationError::OutputFailure)
          return false;
        const auto result = result_from(evaluation.error);
        if (!enqueue_rejection(connection, message, *value,
                               peer.transaction.committed_policy(), result))
          return false;
        std::fprintf(stderr, "gwm: policy rejected commit=%llu result=%u\n",
                     static_cast<unsigned long long>(value->commit_id),
                     static_cast<unsigned>(result));
        return true;
      }
      if (!enqueue_policy(connection, message, *value, evaluation.policy))
        return false;
      ++peer.accepted_commits;
      accepted = true;
      std::fprintf(stderr,
                   "gwm: policy accepted commit=%llu generation=%llu windows=%zu hash=%016llx\n",
                   static_cast<unsigned long long>(value->commit_id),
                   static_cast<unsigned long long>(evaluation.policy.generation),
                   evaluation.policy.windows.size(),
                   static_cast<unsigned long long>(evaluation.policy.hash));
      return true;
    }
    default: return false;
  }
}

int run(const glasswyrm::wm::Options& options) {
  int signal_pipe[2] = {-1, -1};
  if (::pipe2(signal_pipe, O_NONBLOCK | O_CLOEXEC) != 0) {
    std::perror("gwm: signal pipe");
    return 1;
  }
  signal_write_fd = signal_pipe[1];
  const auto previous_int = std::signal(SIGINT, wake_for_signal);
  const auto previous_term = std::signal(SIGTERM, wake_for_signal);

  gwipc_listener_options listener_options{};
  listener_options.struct_size = sizeof(listener_options);
  listener_options.path = options.ipc_socket.c_str();
  listener_options.local_role = GWIPC_ROLE_WINDOW_MANAGER;
  listener_options.accepted_peer_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_PROTOCOL_SERVER);
  listener_options.offered_capabilities = kRequiredCapabilities;
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
    signal_write_fd = -1;
    (void)::close(signal_pipe[0]);
    (void)::close(signal_pipe[1]);
    return 1;
  }
  std::fprintf(stderr, "gwm: listening socket=%s\n", options.ipc_socket.c_str());

  std::unique_ptr<gwipc_connection, ConnectionDeleter> producer;
  PeerState peer;
  bool accepted_any = false;
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
        {signal_pipe[0], POLLIN, 0}};
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
      std::uint8_t bytes[32];
      while (::read(signal_pipe[0], bytes, sizeof(bytes)) > 0) {}
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
        if (options.max_commits &&
            peer.accepted_commits == *options.max_commits)
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
  signal_write_fd = -1;
  (void)::signal(SIGINT, previous_int);
  (void)::signal(SIGTERM, previous_term);
  (void)::close(signal_pipe[0]);
  (void)::close(signal_pipe[1]);
  std::fprintf(stderr, "gwm: stopped\n");
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  glasswyrm::wm::Options options;
  const auto result = glasswyrm::wm::parse_options(argc, argv, options,
                                                   std::cout, std::cerr);
  if (result == glasswyrm::wm::ParseOptionsResult::ExitSuccess) return 0;
  if (result == glasswyrm::wm::ParseOptionsResult::ExitFailure) return 2;
  return run(options);
}
