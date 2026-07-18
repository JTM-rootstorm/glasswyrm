#include "ipc/connection_internal.hpp"

#include "ipc/wire/output_contract.hpp"

#include <span>

namespace gw::ipc {
namespace {

[[nodiscard]] bool output_request(const std::uint16_t type) noexcept {
  return type == GWIPC_MESSAGE_OUTPUT_STATE_QUERY ||
         type == GWIPC_MESSAGE_OUTPUT_CONFIGURATION_COMMIT;
}

[[nodiscard]] bool output_request_identity(
    const std::uint16_t type, const std::span<const std::uint8_t> payload,
    std::uint64_t& request_id) {
  if (type == GWIPC_MESSAGE_OUTPUT_STATE_QUERY) {
    wire::OutputStateQuery value;
    if (wire::decode(payload, value) != wire::CodecStatus::Ok) return false;
    request_id = value.query_id;
    return true;
  }
  if (type == GWIPC_MESSAGE_OUTPUT_CONFIGURATION_COMMIT) {
    wire::OutputConfigurationCommit value;
    if (wire::decode(payload, value) != wire::CodecStatus::Ok) return false;
    request_id = value.configuration_id;
    return true;
  }
  return false;
}

[[nodiscard]] bool acknowledgement_identity(
    const std::span<const std::uint8_t> payload, std::uint64_t& request_id) {
  wire::OutputConfigurationAcknowledged value;
  if (wire::decode(payload, value) != wire::CodecStatus::Ok) return false;
  request_id = value.request_id;
  return true;
}

}  // namespace

gwipc_status prepare_output_request_correlation(
    gwipc_connection& connection, const std::uint16_t type,
    const std::span<const std::uint8_t> payload,
    OutputCorrelationChanges& changes) {
  if (!output_request(type)) return GWIPC_STATUS_OK;
  std::uint64_t request_id = 0;
  if (!output_request_identity(type, payload, request_id))
    return GWIPC_STATUS_PROTOCOL_ERROR;
  if (connection.pending_output_requests.size() >=
      connection.config.maximum_queued_messages)
    return GWIPC_STATUS_LIMIT_EXCEEDED;
  const auto [unused, inserted] = connection.pending_output_requests.emplace(
      changes.sequence, OutputRequestCorrelation{request_id, type});
  static_cast<void>(unused);
  if (!inserted) return GWIPC_STATUS_INVALID_STATE;
  changes.request_inserted = true;
  return GWIPC_STATUS_OK;
}

gwipc_status prepare_output_reply_correlation(
    const gwipc_connection& connection, const std::uint16_t type,
    const std::uint64_t reply_to,
    const std::span<const std::uint8_t> payload,
    OutputCorrelationChanges& changes) {
  if (type != GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED)
    return GWIPC_STATUS_OK;
  std::uint64_t request_id = 0;
  const auto expected = connection.incoming_output_requests.find(reply_to);
  if (!acknowledgement_identity(payload, request_id) ||
      expected == connection.incoming_output_requests.end() ||
      expected->second.request_id != request_id)
    return GWIPC_STATUS_INVALID_STATE;
  changes.acknowledgement = true;
  return GWIPC_STATUS_OK;
}

void rollback_output_request_correlation(
    gwipc_connection& connection,
    const OutputCorrelationChanges& changes) noexcept {
  if (changes.request_inserted)
    connection.pending_output_requests.erase(changes.sequence);
}

void commit_output_reply_correlation(
    gwipc_connection& connection,
    const OutputCorrelationChanges& changes) noexcept {
  if (changes.acknowledgement)
    connection.incoming_output_requests.erase(changes.reply_to);
}

gwipc_status validate_incoming_output_reply_correlation(
    gwipc_connection& connection, const wire::Envelope& envelope,
    const std::span<const std::uint8_t> payload) {
  if (envelope.type != wire::MessageType::OutputConfigurationAcknowledged)
    return GWIPC_STATUS_OK;
  std::uint64_t request_id = 0;
  const auto expected =
      connection.pending_output_requests.find(envelope.reply_to);
  if (!acknowledgement_identity(payload, request_id) ||
      expected == connection.pending_output_requests.end() ||
      expected->second.request_id != request_id) {
    return protocol_failure(
        connection, wire::ProtocolErrorCode::UnexpectedReply, envelope,
        "output acknowledgement does not match query or configuration");
  }
  connection.pending_output_requests.erase(expected);
  return GWIPC_STATUS_OK;
}

gwipc_status track_incoming_output_request_correlation(
    gwipc_connection& connection, const wire::Envelope& envelope,
    const std::span<const std::uint8_t> payload) {
  const auto type = static_cast<std::uint16_t>(envelope.type);
  if (!output_request(type)) return GWIPC_STATUS_OK;
  std::uint64_t request_id = 0;
  if (!output_request_identity(type, payload, request_id)) {
    return protocol_failure(connection,
                            wire::ProtocolErrorCode::MalformedPayload,
                            envelope, "invalid output request correlation");
  }
  if (connection.incoming_output_requests.size() >=
      connection.config.maximum_queued_messages) {
    return protocol_failure(connection, wire::ProtocolErrorCode::LimitExceeded,
                            envelope,
                            "output request tracking limit exceeded",
                            GWIPC_STATUS_LIMIT_EXCEEDED);
  }
  const auto [unused, inserted] = connection.incoming_output_requests.emplace(
      envelope.sequence, OutputRequestCorrelation{request_id, type});
  static_cast<void>(unused);
  if (!inserted) {
    return protocol_failure(connection,
                            wire::ProtocolErrorCode::UnexpectedReply,
                            envelope, "duplicate output request sequence");
  }
  return GWIPC_STATUS_OK;
}

void rollback_incoming_output_request_correlation(
    gwipc_connection& connection, const wire::Envelope& envelope) noexcept {
  const auto type = static_cast<std::uint16_t>(envelope.type);
  if (output_request(type))
    connection.incoming_output_requests.erase(envelope.sequence);
}

}  // namespace gw::ipc
