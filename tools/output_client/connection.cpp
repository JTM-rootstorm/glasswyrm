#include "output_client/internal.hpp"

#include <array>
#include <memory>
#include <poll.h>

namespace glasswyrm::tools::output_client {
namespace {

constexpr std::uint32_t kMaximumPayload = 4096;
constexpr std::uint32_t kMaximumQueuedBytes = 2U * 1024U * 1024U;
constexpr std::uint16_t kMaximumQueuedMessages = 2048;
constexpr unsigned kPollAttempts = 500;

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

bool pump(gwipc_connection *connection, std::string &error) {
  pollfd descriptor{gwipc_connection_fd(connection),
                    gwipc_connection_wanted_poll_events(connection), 0};
  const auto count = ::poll(&descriptor, 1, 10);
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
  value.color = {GWIPC_SDR_COLOR_SPACE_SRGB, GWIPC_TRANSFER_FUNCTION_SRGB,
                 GWIPC_COLOR_PRIMARIES_SRGB, 0, 0, 0, 0};
  return value;
}

gwipc_output_vrr_policy_upsert public_vrr_policy(
    const std::uint64_t output_id, const gwipc_vrr_policy_mode mode) {
  gwipc_output_vrr_policy_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = output_id;
  value.mode = mode;
  return value;
}

} // namespace

Client::~Client() { gwipc_connection_destroy(connection_); }

bool Client::connect(std::string &error) {
  if (connection_)
    return true;
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
  return wait_established(error);
}

bool Client::wait_established(std::string &error) {
  for (unsigned attempt = 0; attempt < kPollAttempts; ++attempt) {
    if (gwipc_connection_get_state(connection_) ==
        GWIPC_CONNECTION_ESTABLISHED)
      return true;
    if (!pump(connection_, error))
      return false;
  }
  error = "timed out establishing the output control connection";
  return false;
}

bool Client::query(const std::uint32_t flags, Snapshot &snapshot,
                   std::string &error) {
  if (!connect(error))
    return false;
  const auto negotiated = gwipc_connection_peer_info(connection_).capabilities;
  constexpr auto vrr_profile =
      GWIPC_CAP_VRR_METADATA | GWIPC_CAP_VRR_POLICY;
  if ((flags & GWIPC_OUTPUT_QUERY_VRR) != 0 &&
      (negotiated & vrr_profile) != vrr_profile) {
    error = "output control peer does not support VRR queries";
    return false;
  }
  const auto effective_flags =
      (negotiated & vrr_profile) == vrr_profile
          ? flags | GWIPC_OUTPUT_QUERY_VRR
          : flags;
  const auto request_id = next_request_id_++;
  gwipc_output_state_query query{};
  query.struct_size = sizeof(query);
  query.query_id = request_id;
  query.flags = effective_flags;
  if (!enqueue_contract(connection_, GWIPC_MESSAGE_OUTPUT_STATE_QUERY,
                        GWIPC_FLAG_ACK_REQUIRED, query,
                        gwipc_contract_encode_output_state_query, error))
    return false;
  SnapshotDecoder decoder(request_id, effective_flags);
  for (unsigned attempt = 0; attempt < kPollAttempts; ++attempt) {
    if (!pump(connection_, error))
      return false;
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
        return false;
      }
      if (!decoder.consume(owned.get(), error))
        return false;
      if (decoder.complete()) {
        snapshot = decoder.take();
        return true;
      }
    }
  }
  error = "timed out waiting for a complete output snapshot";
  return false;
}

bool Client::commit(const Snapshot &snapshot,
                    gwipc_output_configuration_acknowledged &ack,
                    std::string &error) {
  if (!connect(error) || snapshot.outputs.empty())
    return false;
  const auto configuration_id = next_request_id_++;
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
  begin.expected_item_count = snapshot.outputs.size() +
                              (snapshot.vrr_queried
                                   ? snapshot.vrr_policies.size()
                                   : 0U);
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
  for (unsigned attempt = 0; attempt < kPollAttempts; ++attempt) {
    if (!pump(connection_, error))
      return false;
    while (true) {
      gwipc_message *message = nullptr;
      const auto status = gwipc_connection_receive(connection_, &message);
      std::unique_ptr<gwipc_message, decltype(&gwipc_message_destroy)> owned(
          message, gwipc_message_destroy);
      if (status == GWIPC_STATUS_WOULD_BLOCK)
        break;
      if (status != GWIPC_STATUS_OK) {
        error = "could not receive the output configuration acknowledgement";
        return false;
      }
      gwipc_decoded_contract *decoded_raw = nullptr;
      const auto decoded_status =
          gwipc_contract_decode_message(owned.get(), &decoded_raw);
      std::unique_ptr<gwipc_decoded_contract,
                      decltype(&gwipc_decoded_contract_destroy)>
          decoded(decoded_raw, gwipc_decoded_contract_destroy);
      const auto *value = decoded_status == GWIPC_STATUS_OK
                              ? gwipc_decoded_output_configuration_acknowledged(
                                    decoded.get())
                              : nullptr;
      if (!value || value->request_id != configuration_id) {
        error = "control server sent an invalid configuration acknowledgement";
        return false;
      }
      ack = *value;
      return true;
    }
  }
  error = "timed out waiting for output configuration acknowledgement";
  return false;
}

} // namespace glasswyrm::tools::output_client
