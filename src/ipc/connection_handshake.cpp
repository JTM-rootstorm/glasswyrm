#include "ipc/connection_internal.hpp"

#include "ipc/wire/control.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

namespace gw::ipc {
namespace {

gwipc_status reject(gwipc_connection& connection, wire::RejectReason reason,
                    std::string detail, gwipc_status result) {
  std::fprintf(stderr, "gwipc: handshake rejected reason=%u\n",
               static_cast<unsigned>(reason));
  wire::Reject reject_message;
  reject_message.reason = reason;
  reject_message.detail = std::move(detail);
  const auto payload = wire::encode(reject_message);
  if (queue_internal(connection, GWIPC_MESSAGE_REJECT, GWIPC_FLAG_REPLY, 1,
                     payload) != GWIPC_STATUS_OK) {
    set_closed(connection);
    return result;
  }
  connection.state = GWIPC_CONNECTION_REJECTING;
  connection.close_after_flush = true;
  return result;
}

gwipc_status rejection_status(wire::RejectReason reason) noexcept {
  switch (reason) {
    case wire::RejectReason::IncompatibleVersion:
      return GWIPC_STATUS_VERSION_MISMATCH;
    case wire::RejectReason::RoleNotAllowed:
      return GWIPC_STATUS_ROLE_REJECTED;
    case wire::RejectReason::CapabilityMismatch:
      return GWIPC_STATUS_CAPABILITY_MISMATCH;
    case wire::RejectReason::CredentialRejected:
      return GWIPC_STATUS_CREDENTIAL_REJECTED;
    default:
      return GWIPC_STATUS_PROTOCOL_ERROR;
  }
}

bool role_allowed(const Config& config, std::uint16_t role) noexcept {
  return role != 0 && role <= GWIPC_ROLE_DIAGNOSTIC_TOOL &&
         (config.peer_roles & (UINT64_C(1) << role)) != 0;
}

}  // namespace

gwipc_status queue_hello(gwipc_connection& connection) {
  wire::Hello hello;
  hello.sender_role = static_cast<wire::Role>(connection.config.local_role);
  hello.offered_capabilities = connection.config.offered_capabilities;
  hello.required_capabilities = connection.config.required_peer_capabilities;
  hello.maximum_payload = connection.config.maximum_payload;
  hello.maximum_fd_count = connection.config.maximum_fd_count;
  hello.sender_instance_id = connection.config.instance_id;
  hello.name = connection.config.label;
  const auto payload = wire::encode(hello);
  return queue_internal(connection, GWIPC_MESSAGE_HELLO, 0, 0, payload);
}

gwipc_status handle_hello(gwipc_connection& connection,
                          const wire::Envelope& envelope,
                          std::span<const std::uint8_t> payload) {
  wire::Hello hello;
  if (envelope.type != wire::MessageType::Hello || envelope.flags != 0 ||
      envelope.fd_count != 0 ||
      wire::decode(payload, hello) != wire::CodecStatus::Ok)
    return reject(connection, wire::RejectReason::InvalidHello, "invalid hello",
                  GWIPC_STATUS_PROTOCOL_ERROR);
  if (hello.minimum_major > wire::kWireMajor ||
      hello.maximum_major < wire::kWireMajor || hello.minimum_minor > 0 ||
      hello.maximum_minor < wire::kWireMinor)
    return reject(connection, wire::RejectReason::IncompatibleVersion,
                  "wire version mismatch", GWIPC_STATUS_VERSION_MISMATCH);
  const auto role = static_cast<std::uint16_t>(hello.sender_role);
  if (!role_allowed(connection.config, role))
    return reject(connection, wire::RejectReason::RoleNotAllowed,
                  "peer role is not allowed", GWIPC_STATUS_ROLE_REJECTED);
  if (hello.maximum_payload == 0 ||
      hello.maximum_payload > GWIPC_HARD_MAXIMUM_PAYLOAD ||
      hello.maximum_fd_count > GWIPC_HARD_MAXIMUM_FDS ||
      (hello.required_capabilities & ~kKnownCapabilities) != 0)
    return reject(connection, wire::RejectReason::InvalidHello,
                  "invalid offered limits", GWIPC_STATUS_PROTOCOL_ERROR);

  const auto negotiated = hello.offered_capabilities &
                          connection.config.offered_capabilities &
                          kKnownCapabilities;
  if ((hello.required_capabilities & negotiated) !=
          hello.required_capabilities ||
      (connection.config.required_peer_capabilities & negotiated) !=
          connection.config.required_peer_capabilities)
    return reject(connection, wire::RejectReason::CapabilityMismatch,
                  "required capability is unavailable",
                  GWIPC_STATUS_CAPABILITY_MISMATCH);

  connection.peer.role = static_cast<gwipc_role>(role);
  connection.peer.wire_version = {wire::kWireMajor, wire::kWireMinor};
  connection.peer.capabilities = negotiated;
  connection.peer.maximum_payload =
      std::min(connection.config.maximum_payload, hello.maximum_payload);
  connection.peer.maximum_fd_count =
      std::min(connection.config.maximum_fd_count, hello.maximum_fd_count);
  connection.peer.connection_id = connection.assigned_connection_id;

  wire::Welcome welcome;
  welcome.sender_role = static_cast<wire::Role>(connection.config.local_role);
  welcome.negotiated_capabilities = negotiated;
  welcome.negotiated_maximum_payload = connection.peer.maximum_payload;
  welcome.negotiated_maximum_fd_count = connection.peer.maximum_fd_count;
  welcome.connection_id = connection.assigned_connection_id;
  welcome.sender_instance_id = connection.config.instance_id;
  const auto welcome_payload = wire::encode(welcome);
  const auto status = queue_internal(connection, GWIPC_MESSAGE_WELCOME,
                                     GWIPC_FLAG_REPLY, envelope.sequence,
                                     welcome_payload);
  if (status == GWIPC_STATUS_OK) {
    connection.state = GWIPC_CONNECTION_ESTABLISHED;
    std::fprintf(stderr,
                 "gwipc: handshake accepted connection=%llu role=%u wire=1.0\n",
                 static_cast<unsigned long long>(connection.peer.connection_id),
                 static_cast<unsigned>(connection.peer.role));
  }
  return status;
}

gwipc_status handle_welcome(gwipc_connection& connection,
                            const wire::Envelope& envelope,
                            std::span<const std::uint8_t> payload) {
  if (envelope.type == wire::MessageType::Reject) {
    wire::Reject rejection;
    if (envelope.flags != GWIPC_FLAG_REPLY || envelope.reply_to != 1 ||
        envelope.fd_count != 0 ||
        wire::decode(payload, rejection) != wire::CodecStatus::Ok) {
      set_closed(connection);
      return GWIPC_STATUS_PROTOCOL_ERROR;
    }
    set_closed(connection);
    return rejection_status(rejection.reason);
  }

  wire::Welcome welcome;
  if (envelope.type != wire::MessageType::Welcome ||
      envelope.flags != GWIPC_FLAG_REPLY || envelope.reply_to != 1 ||
      envelope.fd_count != 0 ||
      wire::decode(payload, welcome) != wire::CodecStatus::Ok) {
    set_closed(connection);
    return GWIPC_STATUS_PROTOCOL_ERROR;
  }
  const auto role = static_cast<std::uint16_t>(welcome.sender_role);
  if (!role_allowed(connection.config, role)) {
    set_closed(connection);
    return GWIPC_STATUS_ROLE_REJECTED;
  }
  if (welcome.selected_major != wire::kWireMajor ||
      welcome.selected_minor != wire::kWireMinor) {
    set_closed(connection);
    return GWIPC_STATUS_VERSION_MISMATCH;
  }
  if ((welcome.negotiated_capabilities &
       ~connection.config.offered_capabilities) != 0 ||
      (connection.config.required_peer_capabilities &
       welcome.negotiated_capabilities) !=
          connection.config.required_peer_capabilities ||
      welcome.negotiated_maximum_payload == 0 ||
      welcome.negotiated_maximum_payload > connection.config.maximum_payload ||
      welcome.negotiated_maximum_fd_count >
          connection.config.maximum_fd_count ||
      welcome.connection_id == 0) {
    set_closed(connection);
    return GWIPC_STATUS_PROTOCOL_ERROR;
  }
  connection.peer.role = static_cast<gwipc_role>(role);
  connection.peer.wire_version = {welcome.selected_major,
                                  welcome.selected_minor};
  connection.peer.capabilities = welcome.negotiated_capabilities;
  connection.peer.maximum_payload = welcome.negotiated_maximum_payload;
  connection.peer.maximum_fd_count = welcome.negotiated_maximum_fd_count;
  connection.peer.connection_id = welcome.connection_id;
  connection.pending_replies.erase(1);
  connection.state = GWIPC_CONNECTION_ESTABLISHED;
  std::fprintf(stderr,
               "gwipc: handshake accepted connection=%llu role=%u wire=1.0\n",
               static_cast<unsigned long long>(connection.peer.connection_id),
               static_cast<unsigned>(connection.peer.role));
  return GWIPC_STATUS_OK;
}

}  // namespace gw::ipc
