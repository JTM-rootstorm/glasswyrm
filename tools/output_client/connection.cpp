#include "output_client/internal.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <memory>
#include <poll.h>

namespace glasswyrm::tools::output_client {
namespace {

constexpr std::uint32_t kMaximumPayload = 4096;
constexpr std::uint32_t kMaximumQueuedBytes = 2U * 1024U * 1024U;
constexpr std::uint16_t kMaximumQueuedMessages = 2048;
constexpr auto kOperationTimeout = std::chrono::seconds(5);
constexpr auto kPollSlice = std::chrono::milliseconds(10);
constexpr auto kRetryBackoff = std::chrono::milliseconds(10);
using Clock = std::chrono::steady_clock;

int poll_timeout(const Clock::time_point deadline) noexcept {
  const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
      deadline - Clock::now());
  if (remaining <= std::chrono::milliseconds::zero())
    return 0;
  return static_cast<int>(std::min(remaining, kPollSlice).count());
}

template <typename Value>
bool enqueue_contract(gwipc_connection *connection, const std::uint16_t type,
                      const std::uint32_t flags, const Value &value,
                      gwipc_status (*encode)(const Value *,
                                             gwipc_contract_payload **),
                      std::string &error) {
  gwipc_contract_payload *raw = nullptr;
  const auto status = encode(&value, &raw);
  std::unique_ptr<gwipc_contract_payload,
                  decltype(&gwipc_contract_payload_destroy)>
      payload(raw, gwipc_contract_payload_destroy);
  if (status != GWIPC_STATUS_OK) {
    error = std::string("could not encode output request: ") +
            gwipc_status_string(status);
    return false;
  }
  std::size_t size = 0;
  const auto *data = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.flags = flags;
  message.payload = data;
  message.payload_size = size;
  const auto queued = gwipc_connection_enqueue(connection, &message);
  if (queued == GWIPC_STATUS_OK)
    return true;
  error = std::string("could not queue output request: ") +
          gwipc_status_string(queued);
  return false;
}

template <typename Value>
bool enqueue_control(gwipc_connection *connection, const std::uint16_t type,
                     const Value &value,
                     gwipc_status (*encode)(const Value *,
                                            gwipc_control_payload **),
                     std::string &error) {
  gwipc_control_payload *raw = nullptr;
  const auto status = encode(&value, &raw);
  std::unique_ptr<gwipc_control_payload,
                  decltype(&gwipc_control_payload_destroy)>
      payload(raw, gwipc_control_payload_destroy);
  if (status != GWIPC_STATUS_OK) {
    error = std::string("could not encode output snapshot: ") +
            gwipc_status_string(status);
    return false;
  }
  std::size_t size = 0;
  const auto *data = gwipc_control_payload_data(payload.get(), &size);
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.payload = data;
  message.payload_size = size;
  const auto queued = gwipc_connection_enqueue(connection, &message);
  if (queued == GWIPC_STATUS_OK)
    return true;
  error = std::string("could not queue output snapshot: ") +
          gwipc_status_string(queued);
  return false;
}

bool pump(gwipc_connection *connection, const int timeout, std::string &error) {
  pollfd descriptor{gwipc_connection_fd(connection),
                    gwipc_connection_wanted_poll_events(connection), 0};
  const auto count = ::poll(&descriptor, 1, timeout);
  if (count < 0) {
    error = "polling the output control socket failed";
    return false;
  }
  if (count == 0)
    return true;
  const auto status =
      gwipc_connection_process_poll_events(connection, descriptor.revents);
  if (status == GWIPC_STATUS_OK || status == GWIPC_STATUS_WOULD_BLOCK)
    return true;
  error = std::string("output control connection failed: ") +
          gwipc_status_string(status);
  return false;
}

gwipc_output_upsert public_output(const OutputState &state) {
  gwipc_output_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = state.id;
  value.enabled = state.enabled;
  value.logical_x = state.logical_x;
  value.logical_y = state.logical_y;
  value.logical_width = state.logical_width;
  value.logical_height = state.logical_height;
  value.physical_pixel_width = state.physical_width;
  value.physical_pixel_height = state.physical_height;
  value.refresh_millihertz = state.refresh_millihertz;
  value.scale_numerator = state.scale_numerator;
  value.scale_denominator = state.scale_denominator;
  value.transform = state.transform;
  value.color = {GWIPC_SDR_COLOR_SPACE_SRGB,
                 GWIPC_TRANSFER_FUNCTION_SRGB,
                 GWIPC_COLOR_PRIMARIES_SRGB,
                 0,
                 0,
                 0,
                 0};
  return value;
}

gwipc_output_vrr_policy_upsert
public_vrr_policy(const std::uint64_t output_id,
                  const gwipc_vrr_policy_mode mode) {
  gwipc_output_vrr_policy_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = output_id;
  value.mode = mode;
  return value;
}

} // namespace

Client::~Client() { reset_connection(); }

void Client::reset_connection() noexcept {
  gwipc_connection_destroy(connection_);
  connection_ = nullptr;
}

bool Client::connect(std::string &error) {
  if (connection_) {
    if (gwipc_connection_get_state(connection_) == GWIPC_CONNECTION_ESTABLISHED)
      return true;
    reset_connection();
  }
  gwipc_connection_options options{};
  options.struct_size = sizeof(options);
  options.path = socket_path_.c_str();
  options.local_role = GWIPC_ROLE_DIAGNOSTIC_TOOL;
  options.acceptable_server_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_PROTOCOL_SERVER);
  options.offered_capabilities =
      GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE | GWIPC_CAP_OUTPUT_CONTROL |
      GWIPC_CAP_SURFACE_STATE | GWIPC_CAP_WINDOW_LIFECYCLE |
      GWIPC_CAP_SURFACE_OUTPUT_MEMBERSHIP | GWIPC_CAP_SCALE_METADATA;
  options.offered_capabilities |= GWIPC_CAP_VRR_METADATA |
                                  GWIPC_CAP_VRR_POLICY |
                                  GWIPC_CAP_PRESENTATION_TIMING;
  options.required_peer_capabilities = GWIPC_CAP_OUTPUT_CONTROL;
  options.maximum_payload = kMaximumPayload;
  options.maximum_fd_count = 0;
  options.maximum_queued_bytes = kMaximumQueuedBytes;
  options.maximum_queued_messages = kMaximumQueuedMessages;
  options.instance_label = "glasswyrm-output-tool";
  const auto status = gwipc_connection_connect(&options, &connection_);
  if (status != GWIPC_STATUS_OK && status != GWIPC_STATUS_IN_PROGRESS) {
    error = std::string("could not connect to output control socket: ") +
            gwipc_status_string(status);
    return false;
  }
  if (wait_established(error))
    return true;
  reset_connection();
  return false;
}

bool Client::wait_established(std::string &error) {
  const auto deadline = Clock::now() + kOperationTimeout;
  while (Clock::now() < deadline) {
    if (gwipc_connection_get_state(connection_) == GWIPC_CONNECTION_ESTABLISHED)
      return true;
    if (!pump(connection_, poll_timeout(deadline), error))
      return false;
  }
  error = "timed out establishing the output control connection";
  return false;
}

QueryResult Client::query_attempt(const std::uint32_t flags, Snapshot &snapshot,
                                  const bool complete_configuration,
                                  const Clock::time_point deadline) {
  std::string error;
  if (!connect(error))
    return {QueryOutcome::Fatal, std::move(error)};
  const auto negotiated = gwipc_connection_peer_info(connection_).capabilities;
  constexpr auto vrr_profile = GWIPC_CAP_VRR_METADATA | GWIPC_CAP_VRR_POLICY;
  if ((flags & GWIPC_OUTPUT_QUERY_VRR) != 0 &&
      (negotiated & vrr_profile) != vrr_profile) {
    return {QueryOutcome::Fatal,
            "output control peer does not support VRR queries"};
  }
  const auto effective_flags =
      complete_configuration && (negotiated & vrr_profile) == vrr_profile
          ? flags | GWIPC_OUTPUT_QUERY_VRR
          : flags;
  if (next_request_id_ == 0)
    return {QueryOutcome::Fatal, "output query identity was exhausted"};
  const auto request_id = next_request_id_;
  next_request_id_ = request_id == std::numeric_limits<std::uint64_t>::max()
                         ? 0
                         : request_id + 1;
  gwipc_output_state_query query{};
  query.struct_size = sizeof(query);
  query.query_id = request_id;
  query.flags = effective_flags;
  if (!enqueue_contract(connection_, GWIPC_MESSAGE_OUTPUT_STATE_QUERY,
                        GWIPC_FLAG_ACK_REQUIRED, query,
                        gwipc_contract_encode_output_state_query, error)) {
    if (gwipc_connection_get_state(connection_) == GWIPC_CONNECTION_CLOSED)
      reset_connection();
    return {QueryOutcome::Fatal, std::move(error)};
  }
  SnapshotDecoder decoder(request_id, effective_flags);
  while (Clock::now() < deadline) {
    if (!pump(connection_, poll_timeout(deadline), error)) {
      reset_connection();
      return {QueryOutcome::Fatal, std::move(error)};
    }
    while (true) {
      gwipc_message *message = nullptr;
      const auto status = gwipc_connection_receive(connection_, &message);
      std::unique_ptr<gwipc_message, decltype(&gwipc_message_destroy)> owned(
          message, gwipc_message_destroy);
      if (status == GWIPC_STATUS_WOULD_BLOCK)
        break;
      if (status != GWIPC_STATUS_OK) {
        error = std::string("could not receive output snapshot: ") +
                gwipc_status_string(status);
        reset_connection();
        return {QueryOutcome::Fatal, std::move(error)};
      }
      if (!decoder.consume(owned.get(), error)) {
        reset_connection();
        return {QueryOutcome::Fatal, std::move(error)};
      }
      if (decoder.retryable_not_ready())
        return {QueryOutcome::RetryableNotReady,
                "output snapshot is temporarily unavailable"};
      if (decoder.complete()) {
        snapshot = decoder.take();
        return {QueryOutcome::Complete, {}};
      }
    }
  }
  reset_connection();
  return {QueryOutcome::Fatal,
          "timed out waiting for a complete output snapshot"};
}

QueryResult Client::query_once(const std::uint32_t flags, Snapshot &snapshot,
                               const bool complete_configuration) {
  return query_attempt(flags, snapshot, complete_configuration,
                       Clock::now() + kOperationTimeout);
}

bool Client::query(const std::uint32_t flags, Snapshot &snapshot,
                   std::string &error, const bool complete_configuration) {
  const auto deadline = Clock::now() + kOperationTimeout;
  while (Clock::now() < deadline) {
    auto result =
        query_attempt(flags, snapshot, complete_configuration, deadline);
    if (result.outcome == QueryOutcome::Complete) {
      error.clear();
      return true;
    }
    if (result.outcome == QueryOutcome::Fatal) {
      error = std::move(result.detail);
      return false;
    }
    const auto timeout =
        poll_timeout(std::min(deadline, Clock::now() + kRetryBackoff));
    if (timeout > 0)
      (void)::poll(nullptr, 0, timeout);
  }
  error = "timed out waiting for output snapshot readiness";
  return false;
}

bool Client::commit(const Snapshot &snapshot,
                    gwipc_output_configuration_acknowledged &ack,
                    std::string &error) {
  if (!connect(error) || snapshot.outputs.empty())
    return false;
  if (next_request_id_ == 0) {
    error = "output configuration identity was exhausted";
    return false;
  }
  const auto configuration_id = next_request_id_;
  next_request_id_ =
      configuration_id == std::numeric_limits<std::uint64_t>::max()
          ? 0
          : configuration_id + 1;
  gwipc_snapshot_begin begin{};
  begin.struct_size = sizeof(begin);
  begin.snapshot_id = configuration_id;
  begin.domain = GWIPC_SNAPSHOT_OUTPUTS;
  begin.generation = snapshot.generation;
  if (snapshot.vrr_queried &&
      snapshot.vrr_policies.size() != snapshot.outputs.size()) {
    error = "VRR configuration requires one policy for every output";
    return false;
  }
  begin.expected_item_count =
      snapshot.outputs.size() +
      (snapshot.vrr_queried ? snapshot.vrr_policies.size() : 0U);
  if (!enqueue_control(connection_, GWIPC_MESSAGE_SNAPSHOT_BEGIN, begin,
                       gwipc_control_encode_snapshot_begin, error))
    return false;
  for (const auto &[id, state] : snapshot.outputs) {
    (void)id;
    const auto value = public_output(state);
    if (!enqueue_contract(connection_, GWIPC_MESSAGE_OUTPUT_UPSERT,
                          GWIPC_FLAG_SNAPSHOT_ITEM, value,
                          gwipc_contract_encode_output_upsert, error))
      return false;
  }
  if (snapshot.vrr_queried) {
    for (const auto &[output_id, mode] : snapshot.vrr_policies) {
      const auto value = public_vrr_policy(output_id, mode);
      if (!enqueue_contract(connection_, GWIPC_MESSAGE_OUTPUT_VRR_POLICY_UPSERT,
                            GWIPC_FLAG_SNAPSHOT_ITEM, value,
                            gwipc_contract_encode_output_vrr_policy_upsert,
                            error))
        return false;
    }
  }
  gwipc_snapshot_end end{};
  end.struct_size = sizeof(end);
  end.snapshot_id = configuration_id;
  end.generation = snapshot.generation;
  end.actual_item_count = begin.expected_item_count;
  if (!enqueue_control(connection_, GWIPC_MESSAGE_SNAPSHOT_END, end,
                       gwipc_control_encode_snapshot_end, error))
    return false;
  gwipc_output_configuration_commit commit{};
  commit.struct_size = sizeof(commit);
  commit.configuration_id = configuration_id;
  commit.base_generation = snapshot.generation;
  commit.primary_output_id = snapshot.primary_output_id;
  if (!enqueue_contract(connection_, GWIPC_MESSAGE_OUTPUT_CONFIGURATION_COMMIT,
                        GWIPC_FLAG_ACK_REQUIRED, commit,
                        gwipc_contract_encode_output_configuration_commit,
                        error))
    return false;
  const auto deadline = Clock::now() + kOperationTimeout;
  while (Clock::now() < deadline) {
    if (!pump(connection_, poll_timeout(deadline), error)) {
      reset_connection();
      return false;
    }
    while (true) {
      gwipc_message *message = nullptr;
      const auto status = gwipc_connection_receive(connection_, &message);
      std::unique_ptr<gwipc_message, decltype(&gwipc_message_destroy)> owned(
          message, gwipc_message_destroy);
      if (status == GWIPC_STATUS_WOULD_BLOCK)
        break;
      if (status != GWIPC_STATUS_OK) {
        error = "could not receive the output configuration acknowledgement";
        reset_connection();
        return false;
      }
      gwipc_decoded_contract *decoded_raw = nullptr;
      const auto decoded_status =
          gwipc_contract_decode_message(owned.get(), &decoded_raw);
      std::unique_ptr<gwipc_decoded_contract,
                      decltype(&gwipc_decoded_contract_destroy)>
          decoded(decoded_raw, gwipc_decoded_contract_destroy);
      const auto *value =
          decoded_status == GWIPC_STATUS_OK
              ? gwipc_decoded_output_configuration_acknowledged(decoded.get())
              : nullptr;
      if (!value || value->request_id != configuration_id) {
        error = "control server sent an invalid configuration acknowledgement";
        return false;
      }
      ack = *value;
      return true;
    }
  }
  reset_connection();
  error = "timed out waiting for output configuration acknowledgement";
  return false;
}

} // namespace glasswyrm::tools::output_client
