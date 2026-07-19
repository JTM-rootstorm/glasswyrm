#include "ipc/connection_internal.hpp"

#include "ipc/wire/compositor_contract.hpp"
#include "ipc/wire/control.hpp"
#include "ipc/wire/input_contract.hpp"
#include "ipc/wire/policy_contract.hpp"
#include "ipc/wire/session_contract.hpp"
#include "ipc/wire/vrr_contract.hpp"

#include <algorithm>
#include <new>
#include <span>

namespace gw::ipc {

gwipc_status prepare_output_request_correlation(
    gwipc_connection& connection, std::uint16_t type,
    std::span<const std::uint8_t> payload,
    OutputCorrelationChanges& changes);
gwipc_status prepare_output_reply_correlation(
    const gwipc_connection& connection, std::uint16_t type,
    std::uint64_t reply_to, std::span<const std::uint8_t> payload,
    OutputCorrelationChanges& changes);
void rollback_output_request_correlation(
    gwipc_connection& connection,
    const OutputCorrelationChanges& changes) noexcept;
void commit_output_reply_correlation(
    gwipc_connection& connection,
    const OutputCorrelationChanges& changes) noexcept;
gwipc_status validate_incoming_output_reply_correlation(
    gwipc_connection& connection, const wire::Envelope& envelope,
    std::span<const std::uint8_t> payload);
gwipc_status track_incoming_output_request_correlation(
    gwipc_connection& connection, const wire::Envelope& envelope,
    std::span<const std::uint8_t> payload);
void rollback_incoming_output_request_correlation(
    gwipc_connection& connection, const wire::Envelope& envelope) noexcept;

namespace {

struct CorrelationChanges {
  std::uint64_t sequence{};
  std::uint64_t reply_to{};
  bool ping_inserted{};
  bool frame_commit_inserted{};
  bool policy_commit_inserted{};
  bool synthetic_input_inserted{};
  bool session_state_inserted{};
  bool frame_acknowledged{};
  bool policy_acknowledged{};
  bool synthetic_input_acknowledged{};
  bool session_state_acknowledged{};
  std::uint64_t session_generation{};
  OutputCorrelationChanges output;
};

bool synthetic_input_id(std::uint16_t type,
                        std::span<const std::uint8_t> payload,
                        std::uint64_t& input_id) {
  switch (type) {
    case GWIPC_MESSAGE_SYNTHETIC_MOTION: {
      wire::SyntheticMotion value;
      if (wire::decode(payload, value) != wire::CodecStatus::Ok) return false;
      input_id = value.input_id;
      return true;
    }
    case GWIPC_MESSAGE_SYNTHETIC_BUTTON: {
      wire::SyntheticButton value;
      if (wire::decode(payload, value) != wire::CodecStatus::Ok) return false;
      input_id = value.input_id;
      return true;
    }
    case GWIPC_MESSAGE_SYNTHETIC_KEY: {
      wire::SyntheticKey value;
      if (wire::decode(payload, value) != wire::CodecStatus::Ok) return false;
      input_id = value.input_id;
      return true;
    }
    case GWIPC_MESSAGE_SYNTHETIC_BARRIER: {
      wire::SyntheticBarrier value;
      if (wire::decode(payload, value) != wire::CodecStatus::Ok) return false;
      input_id = value.input_id;
      return true;
    }
    default:
      return false;
  }
}

gwipc_status prepare_request(gwipc_connection& connection,
                             const gwipc_outgoing_message& message,
                             std::span<const std::uint8_t> payload,
                             CorrelationChanges& changes) {
  changes.output.sequence = changes.sequence;
  changes.output.reply_to = changes.reply_to;
  const auto output_status = prepare_output_request_correlation(
      connection, message.type, payload, changes.output);
  if (output_status != GWIPC_STATUS_OK) return output_status;
  if (message.type == GWIPC_MESSAGE_PING) {
    wire::Ping ping;
    if (wire::decode(payload, ping) != wire::CodecStatus::Ok)
      return GWIPC_STATUS_PROTOCOL_ERROR;
    connection.pending_ping_nonces.emplace(changes.sequence, ping.nonce);
    changes.ping_inserted = true;
  }
  if (message.type == GWIPC_MESSAGE_FRAME_COMMIT) {
    wire::FrameCommit commit;
    if (wire::decode(payload, commit) != wire::CodecStatus::Ok)
      return GWIPC_STATUS_PROTOCOL_ERROR;
    if (connection.pending_frame_commits.size() >=
        connection.config.maximum_queued_messages)
      return GWIPC_STATUS_LIMIT_EXCEEDED;
    connection.pending_frame_commits.emplace(changes.sequence,
                                              commit.commit_id);
    changes.frame_commit_inserted = true;
  }
  if (message.type == GWIPC_MESSAGE_POLICY_COMMIT) {
    wire::PolicyCommit commit;
    if (wire::decode(payload, commit) != wire::CodecStatus::Ok)
      return GWIPC_STATUS_PROTOCOL_ERROR;
    if (connection.pending_policy_commits.size() >=
        connection.config.maximum_queued_messages)
      return GWIPC_STATUS_LIMIT_EXCEEDED;
    connection.pending_policy_commits.emplace(changes.sequence,
                                               commit.commit_id);
    changes.policy_commit_inserted = true;
  }
  std::uint64_t input_id = 0;
  if (synthetic_input_id(message.type, payload, input_id)) {
    if (connection.pending_synthetic_inputs.size() >=
        connection.config.maximum_queued_messages)
      return GWIPC_STATUS_LIMIT_EXCEEDED;
    connection.pending_synthetic_inputs.emplace(changes.sequence, input_id);
    changes.synthetic_input_inserted = true;
  }
  if (message.type == GWIPC_MESSAGE_SESSION_STATE_CHANGE) {
    wire::SessionStateChange value;
    if (wire::decode(payload, value) != wire::CodecStatus::Ok)
      return GWIPC_STATUS_PROTOCOL_ERROR;
    if (value.generation <= connection.last_outgoing_session_generation)
      return GWIPC_STATUS_INVALID_STATE;
    if (connection.pending_session_states.size() >=
        connection.config.maximum_queued_messages)
      return GWIPC_STATUS_LIMIT_EXCEEDED;
    connection.pending_session_states.emplace(
        changes.sequence,
        SessionCorrelation{value.generation,
                           static_cast<std::uint16_t>(value.state)});
    changes.session_state_inserted = true;
    changes.session_generation = value.generation;
  }
  return GWIPC_STATUS_OK;
}

gwipc_status prepare_reply(gwipc_connection& connection,
                           const gwipc_outgoing_message& message,
                           std::span<const std::uint8_t> payload,
                           CorrelationChanges& changes) {
  const auto output_status = prepare_output_reply_correlation(
      connection, message.type, message.reply_to, payload, changes.output);
  if (output_status != GWIPC_STATUS_OK) return output_status;
  if (message.type == GWIPC_MESSAGE_FRAME_ACKNOWLEDGED) {
    wire::FrameAcknowledged value;
    const auto expected = connection.incoming_frame_commits.find(
        message.reply_to);
    if (wire::decode(payload, value) != wire::CodecStatus::Ok ||
        expected == connection.incoming_frame_commits.end() ||
        expected->second != value.commit_id)
      return GWIPC_STATUS_INVALID_STATE;
    changes.frame_acknowledged = true;
  }
  if (message.type == GWIPC_MESSAGE_OUTPUT_VRR_STATE_UPSERT) {
    wire::OutputVrrStateUpsert value;
    const auto expected = connection.incoming_frame_commits.find(
        message.reply_to);
    if (wire::decode(payload, value) != wire::CodecStatus::Ok ||
        expected == connection.incoming_frame_commits.end() ||
        expected->second != value.last_commit_id)
      return GWIPC_STATUS_INVALID_STATE;
  }
  if (message.type == GWIPC_MESSAGE_POLICY_ACKNOWLEDGED) {
    wire::PolicyAcknowledged value;
    const auto expected = connection.incoming_policy_commits.find(
        message.reply_to);
    if (wire::decode(payload, value) != wire::CodecStatus::Ok ||
        expected == connection.incoming_policy_commits.end() ||
        expected->second != value.commit_id)
      return GWIPC_STATUS_INVALID_STATE;
    changes.policy_acknowledged = true;
  }
  if (message.type == GWIPC_MESSAGE_SYNTHETIC_INPUT_ACKNOWLEDGED) {
    wire::SyntheticInputAcknowledged value;
    const auto expected = connection.incoming_synthetic_inputs.find(
        message.reply_to);
    if (wire::decode(payload, value) != wire::CodecStatus::Ok ||
        expected == connection.incoming_synthetic_inputs.end() ||
        expected->second != value.input_id)
      return GWIPC_STATUS_INVALID_STATE;
    changes.synthetic_input_acknowledged = true;
  }
  if (message.type == GWIPC_MESSAGE_SESSION_STATE_ACKNOWLEDGED) {
    wire::SessionStateAcknowledged value;
    const auto expected =
        connection.incoming_session_states.find(message.reply_to);
    if (wire::decode(payload, value) != wire::CodecStatus::Ok ||
        expected == connection.incoming_session_states.end() ||
        expected->second.generation != value.generation ||
        expected->second.state != static_cast<std::uint16_t>(value.state))
      return GWIPC_STATUS_INVALID_STATE;
    changes.session_state_acknowledged = true;
  }
  return GWIPC_STATUS_OK;
}

void rollback_requests(gwipc_connection& connection,
                       const CorrelationChanges& changes) noexcept {
  rollback_output_request_correlation(connection, changes.output);
  if (changes.ping_inserted)
    connection.pending_ping_nonces.erase(changes.sequence);
  if (changes.frame_commit_inserted)
    connection.pending_frame_commits.erase(changes.sequence);
  if (changes.policy_commit_inserted)
    connection.pending_policy_commits.erase(changes.sequence);
  if (changes.synthetic_input_inserted)
    connection.pending_synthetic_inputs.erase(changes.sequence);
  if (changes.session_state_inserted)
    connection.pending_session_states.erase(changes.sequence);
}

void commit_replies(gwipc_connection& connection,
                    const CorrelationChanges& changes) noexcept {
  commit_output_reply_correlation(connection, changes.output);
  if (changes.frame_acknowledged)
    connection.incoming_frame_commits.erase(changes.reply_to);
  if (changes.policy_acknowledged)
    connection.incoming_policy_commits.erase(changes.reply_to);
  if (changes.synthetic_input_acknowledged)
    connection.incoming_synthetic_inputs.erase(changes.reply_to);
  if (changes.session_state_acknowledged)
    connection.incoming_session_states.erase(changes.reply_to);
}

bool valid_outgoing_message(const gwipc_outgoing_message& message) noexcept {
  return message.struct_size >= sizeof(message) &&
         std::none_of(std::begin(message.reserved), std::end(message.reserved),
                      [](auto value) { return value != 0; }) &&
         (message.payload_size == 0 || message.payload) &&
         (message.fd_count == 0 || message.fds);
}

bool valid_outgoing_flags(const gwipc_outgoing_message& message) noexcept {
  return (message.flags & ~wire::kKnownMessageFlags) == 0 &&
         ((message.flags & GWIPC_FLAG_ERROR) == 0 ||
          (message.flags & GWIPC_FLAG_REPLY) != 0) &&
         (((message.flags & GWIPC_FLAG_REPLY) != 0) ==
          (message.reply_to != 0));
}

}  // namespace

gwipc_status validate_incoming_reply(
    gwipc_connection& connection, const wire::Envelope& envelope,
    std::span<const std::uint8_t> payload) {
  const auto output_status = validate_incoming_output_reply_correlation(
      connection, envelope, payload);
  if (output_status != GWIPC_STATUS_OK) return output_status;
  if (envelope.type == wire::MessageType::Pong) {
    wire::Pong value;
    const auto expected = connection.pending_ping_nonces.find(envelope.reply_to);
    if (expected == connection.pending_ping_nonces.end() ||
        wire::decode(payload, value) != wire::CodecStatus::Ok ||
        value.nonce != expected->second)
      return protocol_failure(connection, wire::ProtocolErrorCode::UnexpectedReply,
                              envelope, "unexpected pong reply");
    connection.pending_ping_nonces.erase(expected);
  }
  if (envelope.type == wire::MessageType::FrameAcknowledged) {
    wire::FrameAcknowledged value;
    const auto expected = connection.pending_frame_commits.find(envelope.reply_to);
    if (expected == connection.pending_frame_commits.end() ||
        wire::decode(payload, value) != wire::CodecStatus::Ok ||
        value.commit_id != expected->second)
      return protocol_failure(connection, wire::ProtocolErrorCode::UnexpectedReply,
                              envelope,
                              "frame acknowledgement does not match commit");
    connection.pending_frame_commits.erase(expected);
  }
  if (envelope.type == wire::MessageType::OutputVrrStateUpsert) {
    wire::OutputVrrStateUpsert value;
    const auto expected =
        connection.pending_frame_commits.find(envelope.reply_to);
    if (expected == connection.pending_frame_commits.end() ||
        wire::decode(payload, value) != wire::CodecStatus::Ok ||
        value.last_commit_id != expected->second)
      return protocol_failure(
          connection, wire::ProtocolErrorCode::UnexpectedReply, envelope,
          "VRR state does not match pending frame commit");
    return GWIPC_STATUS_OK;
  }
  if (envelope.type == wire::MessageType::PolicyAcknowledged) {
    wire::PolicyAcknowledged value;
    const auto expected = connection.pending_policy_commits.find(envelope.reply_to);
    if (expected == connection.pending_policy_commits.end() ||
        wire::decode(payload, value) != wire::CodecStatus::Ok ||
        value.commit_id != expected->second)
      return protocol_failure(connection, wire::ProtocolErrorCode::UnexpectedReply,
                              envelope,
                              "policy acknowledgement does not match commit");
    connection.pending_policy_commits.erase(expected);
  }
  if (envelope.type == wire::MessageType::SyntheticInputAcknowledged) {
    wire::SyntheticInputAcknowledged value;
    const auto expected =
        connection.pending_synthetic_inputs.find(envelope.reply_to);
    if (expected == connection.pending_synthetic_inputs.end() ||
        wire::decode(payload, value) != wire::CodecStatus::Ok ||
        value.input_id != expected->second)
      return protocol_failure(
          connection, wire::ProtocolErrorCode::UnexpectedReply, envelope,
          "synthetic input acknowledgement does not match input");
    connection.pending_synthetic_inputs.erase(expected);
  }
  if (envelope.type == wire::MessageType::SessionStateAcknowledged) {
    wire::SessionStateAcknowledged value;
    const auto expected =
        connection.pending_session_states.find(envelope.reply_to);
    if (expected == connection.pending_session_states.end() ||
        wire::decode(payload, value) != wire::CodecStatus::Ok ||
        expected->second.generation != value.generation ||
        expected->second.state != static_cast<std::uint16_t>(value.state))
      return protocol_failure(
          connection, wire::ProtocolErrorCode::UnexpectedReply, envelope,
          "session state acknowledgement does not match request");
    connection.pending_session_states.erase(expected);
  }
  const bool protocol_error = envelope.type == wire::MessageType::ProtocolError;
  if (connection.pending_replies.erase(envelope.reply_to) == 0 &&
      !(protocol_error && envelope.reply_to > 0 &&
        envelope.reply_to < connection.next_send_sequence))
    return protocol_failure(connection, wire::ProtocolErrorCode::UnexpectedReply,
                            envelope, "unexpected reply correlation");
  return GWIPC_STATUS_OK;
}

gwipc_status track_incoming_request(
    gwipc_connection& connection, const wire::Envelope& envelope,
    std::span<const std::uint8_t> payload) {
  const auto output_status = track_incoming_output_request_correlation(
      connection, envelope, payload);
  if (output_status != GWIPC_STATUS_OK) return output_status;
  if (envelope.type == wire::MessageType::FrameCommit) {
    wire::FrameCommit value;
    if (wire::decode(payload, value) != wire::CodecStatus::Ok ||
        connection.incoming_frame_commits.size() >=
            connection.config.maximum_queued_messages)
      return protocol_failure(connection, wire::ProtocolErrorCode::LimitExceeded,
                              envelope,
                              "frame acknowledgement tracking limit exceeded",
                              GWIPC_STATUS_LIMIT_EXCEEDED);
    connection.incoming_frame_commits.emplace(envelope.sequence,
                                               value.commit_id);
  }
  if (envelope.type == wire::MessageType::PolicyCommit) {
    wire::PolicyCommit value;
    if (wire::decode(payload, value) != wire::CodecStatus::Ok ||
        connection.incoming_policy_commits.size() >=
            connection.config.maximum_queued_messages)
      return protocol_failure(connection, wire::ProtocolErrorCode::LimitExceeded,
                              envelope,
                              "policy acknowledgement tracking limit exceeded",
                              GWIPC_STATUS_LIMIT_EXCEEDED);
    connection.incoming_policy_commits.emplace(envelope.sequence,
                                                value.commit_id);
  }
  std::uint64_t input_id = 0;
  if (synthetic_input_id(static_cast<std::uint16_t>(envelope.type), payload,
                         input_id)) {
    if (connection.incoming_synthetic_inputs.size() >=
        connection.config.maximum_queued_messages)
      return protocol_failure(
          connection, wire::ProtocolErrorCode::LimitExceeded, envelope,
          "synthetic input acknowledgement tracking limit exceeded",
          GWIPC_STATUS_LIMIT_EXCEEDED);
    connection.incoming_synthetic_inputs.emplace(envelope.sequence, input_id);
  }
  if (envelope.type == wire::MessageType::SessionStateChange) {
    wire::SessionStateChange value;
    if (wire::decode(payload, value) != wire::CodecStatus::Ok ||
        value.generation <= connection.last_incoming_session_generation)
      return protocol_failure(connection,
                              wire::ProtocolErrorCode::MalformedPayload,
                              envelope,
                              "session generation is not strictly increasing");
    if (connection.incoming_session_states.size() >=
        connection.config.maximum_queued_messages)
      return protocol_failure(connection, wire::ProtocolErrorCode::LimitExceeded,
                              envelope,
                              "session acknowledgement tracking limit exceeded",
                              GWIPC_STATUS_LIMIT_EXCEEDED);
    connection.incoming_session_states.emplace(
        envelope.sequence,
        SessionCorrelation{value.generation,
                           static_cast<std::uint16_t>(value.state)});
  }
  return GWIPC_STATUS_OK;
}

void rollback_incoming_request(gwipc_connection& connection,
                               const wire::Envelope& envelope) noexcept {
  rollback_incoming_output_request_correlation(connection, envelope);
  if (envelope.type == wire::MessageType::FrameCommit)
    connection.incoming_frame_commits.erase(envelope.sequence);
  if (envelope.type == wire::MessageType::PolicyCommit)
    connection.incoming_policy_commits.erase(envelope.sequence);
  if (envelope.type == wire::MessageType::SyntheticMotion ||
      envelope.type == wire::MessageType::SyntheticButton ||
      envelope.type == wire::MessageType::SyntheticKey ||
      envelope.type == wire::MessageType::SyntheticBarrier)
    connection.incoming_synthetic_inputs.erase(envelope.sequence);
  if (envelope.type == wire::MessageType::SessionStateChange)
    connection.incoming_session_states.erase(envelope.sequence);
}

void commit_incoming_request(gwipc_connection& connection,
                             const wire::Envelope& envelope,
                             std::span<const std::uint8_t> payload) noexcept {
  if (envelope.type != wire::MessageType::SessionStateChange) return;
  wire::SessionStateChange value;
  if (wire::decode(payload, value) == wire::CodecStatus::Ok)
    connection.last_incoming_session_generation = value.generation;
}

gwipc_status enqueue_with_sequence(gwipc_connection& connection,
                                   const gwipc_outgoing_message& message,
                                   std::uint64_t& out_sequence) {
  if (!valid_outgoing_message(message)) return GWIPC_STATUS_INVALID_ARGUMENT;
  if (connection.state != GWIPC_CONNECTION_ESTABLISHED)
    return GWIPC_STATUS_INVALID_STATE;
  if (!valid_outgoing_flags(message)) return GWIPC_STATUS_INVALID_ARGUMENT;

  const auto payload = std::span(message.payload, message.payload_size);
  const auto fds = std::span(message.fds, message.fd_count);
  auto snapshot = connection.outgoing_snapshot;
  const auto validation = validate_application(
      connection, message.type, message.flags, payload, fds, snapshot,
      MessageDirection::Outgoing);
  if (validation != GWIPC_STATUS_OK) return validation;

  CorrelationChanges changes;
  changes.sequence = connection.next_send_sequence;
  changes.reply_to = message.reply_to;
  auto status = prepare_request(connection, message, payload, changes);
  if (status == GWIPC_STATUS_OK)
    status = prepare_reply(connection, message, payload, changes);
  if (status != GWIPC_STATUS_OK) {
    rollback_requests(connection, changes);
    return status;
  }
  try {
    status = queue_internal(connection, message.type, message.flags,
                            message.reply_to, payload, fds);
  } catch (...) {
    rollback_requests(connection, changes);
    throw;
  }
  if (status != GWIPC_STATUS_OK) {
    rollback_requests(connection, changes);
    return status;
  }
  connection.outgoing_snapshot = snapshot;
  if (changes.session_state_inserted)
    connection.last_outgoing_session_generation = changes.session_generation;
  commit_replies(connection, changes);
  out_sequence = changes.sequence;
  return GWIPC_STATUS_OK;
}

}  // namespace gw::ipc
