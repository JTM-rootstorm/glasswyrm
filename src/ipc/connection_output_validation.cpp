#include "ipc/connection_internal.hpp"

#include "ipc/wire/control.hpp"
#include "ipc/wire/output_contract.hpp"

#include <span>
#include <utility>

namespace gw::ipc {
namespace {

struct Roles {
  gwipc_role sender;
  gwipc_role receiver;
};

[[nodiscard]] Roles message_roles(const gwipc_connection& connection,
                                  const MessageDirection direction) noexcept {
  Roles roles{connection.config.local_role, connection.peer.role};
  if (direction == MessageDirection::Incoming)
    std::swap(roles.sender, roles.receiver);
  return roles;
}

[[nodiscard]] bool has_capabilities(const gwipc_connection& connection,
                                    const std::uint64_t required) noexcept {
  return (connection.peer.capabilities & required) == required;
}

[[nodiscard]] bool exact_roles(const Roles roles, const gwipc_role sender,
                               const gwipc_role receiver) noexcept {
  return roles.sender == sender && roles.receiver == receiver;
}

[[nodiscard]] gwipc_status validate_roles_and_capabilities(
    const gwipc_connection& connection, const std::uint16_t type,
    const MessageDirection direction) noexcept {
  const auto roles = message_roles(connection, direction);
  std::uint64_t required = 0;
  bool valid = false;
  switch (type) {
    case GWIPC_MESSAGE_OUTPUT_DESCRIPTOR_UPSERT:
    case GWIPC_MESSAGE_OUTPUT_MODE_UPSERT:
      valid = exact_roles(roles, GWIPC_ROLE_COMPOSITOR,
                          GWIPC_ROLE_PROTOCOL_SERVER);
      required = GWIPC_CAP_OUTPUT_MANAGEMENT;
      if (!valid) {
        valid = exact_roles(roles, GWIPC_ROLE_PROTOCOL_SERVER,
                            GWIPC_ROLE_DIAGNOSTIC_TOOL);
        required = GWIPC_CAP_OUTPUT_CONTROL;
      }
      break;
    case GWIPC_MESSAGE_SURFACE_OUTPUT_STATE:
      valid = exact_roles(roles, GWIPC_ROLE_PROTOCOL_SERVER,
                          GWIPC_ROLE_COMPOSITOR);
      required = GWIPC_CAP_SURFACE_OUTPUT_MEMBERSHIP |
                 GWIPC_CAP_SCALE_METADATA;
      if (!valid) {
        valid = exact_roles(roles, GWIPC_ROLE_PROTOCOL_SERVER,
                            GWIPC_ROLE_DIAGNOSTIC_TOOL);
        required = GWIPC_CAP_OUTPUT_CONTROL |
                   GWIPC_CAP_SURFACE_OUTPUT_MEMBERSHIP |
                   GWIPC_CAP_SCALE_METADATA;
      }
      break;
    case GWIPC_MESSAGE_POLICY_OUTPUT_UPSERT:
    case GWIPC_MESSAGE_POLICY_WINDOW_OUTPUT_HINT:
      valid = exact_roles(roles, GWIPC_ROLE_PROTOCOL_SERVER,
                          GWIPC_ROLE_WINDOW_MANAGER);
      required = GWIPC_CAP_WINDOW_POLICY | GWIPC_CAP_MULTI_OUTPUT_POLICY |
                 GWIPC_CAP_SCALE_METADATA;
      break;
    case GWIPC_MESSAGE_OUTPUT_STATE_QUERY:
      valid = exact_roles(roles, GWIPC_ROLE_PROTOCOL_SERVER,
                          GWIPC_ROLE_COMPOSITOR);
      required = GWIPC_CAP_OUTPUT_MANAGEMENT;
      if (!valid) {
        valid = exact_roles(roles, GWIPC_ROLE_DIAGNOSTIC_TOOL,
                            GWIPC_ROLE_PROTOCOL_SERVER);
        required = GWIPC_CAP_OUTPUT_CONTROL;
      }
      break;
    case GWIPC_MESSAGE_OUTPUT_CONFIGURATION_COMMIT:
      valid = exact_roles(roles, GWIPC_ROLE_DIAGNOSTIC_TOOL,
                          GWIPC_ROLE_PROTOCOL_SERVER);
      required = GWIPC_CAP_OUTPUT_CONTROL;
      break;
    case GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED:
      valid = exact_roles(roles, GWIPC_ROLE_COMPOSITOR,
                          GWIPC_ROLE_PROTOCOL_SERVER);
      required = GWIPC_CAP_OUTPUT_MANAGEMENT;
      if (!valid) {
        valid = exact_roles(roles, GWIPC_ROLE_PROTOCOL_SERVER,
                            GWIPC_ROLE_DIAGNOSTIC_TOOL);
        required = GWIPC_CAP_OUTPUT_CONTROL;
      }
      break;
    default:
      return GWIPC_STATUS_INVALID_ARGUMENT;
  }
  if (!valid) return GWIPC_STATUS_PROTOCOL_ERROR;
  return has_capabilities(connection, required)
             ? GWIPC_STATUS_OK
             : GWIPC_STATUS_CAPABILITY_MISMATCH;
}

[[nodiscard]] gwipc_status validate_flags_and_snapshot(
    const gwipc_connection& connection, const std::uint16_t type,
    const std::uint32_t flags, const SnapshotState& snapshot,
    const MessageDirection direction) noexcept {
  switch (type) {
    case GWIPC_MESSAGE_OUTPUT_DESCRIPTOR_UPSERT:
    case GWIPC_MESSAGE_OUTPUT_MODE_UPSERT:
      return flags == GWIPC_FLAG_SNAPSHOT_ITEM && snapshot.active &&
                     snapshot.domain == static_cast<std::uint16_t>(
                                            wire::SnapshotDomain::Outputs)
                 ? GWIPC_STATUS_OK
                 : GWIPC_STATUS_PROTOCOL_ERROR;
    case GWIPC_MESSAGE_SURFACE_OUTPUT_STATE:
      return flags == GWIPC_FLAG_SNAPSHOT_ITEM && snapshot.active &&
                     (snapshot.domain == static_cast<std::uint16_t>(
                                             wire::SnapshotDomain::CompleteSession) ||
                      (snapshot.domain == static_cast<std::uint16_t>(
                                              wire::SnapshotDomain::Outputs) &&
                       exact_roles(message_roles(connection, direction),
                                   GWIPC_ROLE_PROTOCOL_SERVER,
                                   GWIPC_ROLE_DIAGNOSTIC_TOOL)))
                 ? GWIPC_STATUS_OK
                 : GWIPC_STATUS_PROTOCOL_ERROR;
    case GWIPC_MESSAGE_POLICY_OUTPUT_UPSERT:
    case GWIPC_MESSAGE_POLICY_WINDOW_OUTPUT_HINT:
      return flags == GWIPC_FLAG_SNAPSHOT_ITEM && snapshot.active &&
                     snapshot.domain == static_cast<std::uint16_t>(
                                            wire::SnapshotDomain::WindowPolicy)
                 ? GWIPC_STATUS_OK
                 : GWIPC_STATUS_PROTOCOL_ERROR;
    case GWIPC_MESSAGE_OUTPUT_STATE_QUERY:
    case GWIPC_MESSAGE_OUTPUT_CONFIGURATION_COMMIT:
      return flags == GWIPC_FLAG_ACK_REQUIRED && !snapshot.active
                 ? GWIPC_STATUS_OK
                 : GWIPC_STATUS_PROTOCOL_ERROR;
    case GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED:
      return flags == GWIPC_FLAG_REPLY && !snapshot.active
                 ? GWIPC_STATUS_OK
                 : GWIPC_STATUS_PROTOCOL_ERROR;
    default:
      return GWIPC_STATUS_INVALID_ARGUMENT;
  }
}

[[nodiscard]] bool valid_payload(const std::uint16_t type,
                                 const std::span<const std::uint8_t> payload) {
  switch (type) {
    case GWIPC_MESSAGE_OUTPUT_DESCRIPTOR_UPSERT: {
      wire::OutputDescriptorUpsert value;
      return wire::decode(payload, value) == wire::CodecStatus::Ok;
    }
    case GWIPC_MESSAGE_OUTPUT_MODE_UPSERT: {
      wire::OutputModeUpsert value;
      return wire::decode(payload, value) == wire::CodecStatus::Ok;
    }
    case GWIPC_MESSAGE_SURFACE_OUTPUT_STATE: {
      wire::SurfaceOutputState value;
      return wire::decode(payload, value) == wire::CodecStatus::Ok;
    }
    case GWIPC_MESSAGE_POLICY_OUTPUT_UPSERT: {
      wire::PolicyOutputUpsert value;
      return wire::decode(payload, value) == wire::CodecStatus::Ok;
    }
    case GWIPC_MESSAGE_POLICY_WINDOW_OUTPUT_HINT: {
      wire::PolicyWindowOutputHint value;
      return wire::decode(payload, value) == wire::CodecStatus::Ok;
    }
    case GWIPC_MESSAGE_OUTPUT_STATE_QUERY: {
      wire::OutputStateQuery value;
      return wire::decode(payload, value) == wire::CodecStatus::Ok;
    }
    case GWIPC_MESSAGE_OUTPUT_CONFIGURATION_COMMIT: {
      wire::OutputConfigurationCommit value;
      return wire::decode(payload, value) == wire::CodecStatus::Ok;
    }
    case GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED: {
      wire::OutputConfigurationAcknowledged value;
      return wire::decode(payload, value) == wire::CodecStatus::Ok;
    }
    default:
      return false;
  }
}

}  // namespace

bool output_extension_message(const std::uint16_t type) noexcept {
  switch (type) {
    case GWIPC_MESSAGE_OUTPUT_DESCRIPTOR_UPSERT:
    case GWIPC_MESSAGE_OUTPUT_MODE_UPSERT:
    case GWIPC_MESSAGE_SURFACE_OUTPUT_STATE:
    case GWIPC_MESSAGE_POLICY_OUTPUT_UPSERT:
    case GWIPC_MESSAGE_POLICY_WINDOW_OUTPUT_HINT:
    case GWIPC_MESSAGE_OUTPUT_STATE_QUERY:
    case GWIPC_MESSAGE_OUTPUT_CONFIGURATION_COMMIT:
    case GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED:
      return true;
    default:
      return false;
  }
}

gwipc_status validate_output_extension(
    const gwipc_connection& connection, const std::uint16_t type,
    const std::uint32_t flags, const std::span<const std::uint8_t> payload,
    const std::span<const int> fds, const SnapshotState& snapshot,
    const MessageDirection direction) {
  if (!output_extension_message(type)) return GWIPC_STATUS_INVALID_ARGUMENT;
  if (!fds.empty()) return GWIPC_STATUS_PROTOCOL_ERROR;
  auto status = validate_roles_and_capabilities(connection, type, direction);
  if (status != GWIPC_STATUS_OK) return status;
  status =
      validate_flags_and_snapshot(connection, type, flags, snapshot, direction);
  if (status != GWIPC_STATUS_OK) return status;
  return valid_payload(type, payload) ? GWIPC_STATUS_OK
                                      : GWIPC_STATUS_PROTOCOL_ERROR;
}

}  // namespace gw::ipc
