#include "ipc/endpoint.hpp"
#include "ipc/internal.hpp"

#include <sys/random.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

namespace gw::ipc {
namespace {

void log_listener_path(const std::string& path, gwipc_role role) noexcept {
  std::array<char, 129> escaped{};
  const auto count = std::min(path.size(), escaped.size() - 1);
  for (std::size_t index = 0; index < count; ++index) {
    const auto byte = static_cast<unsigned char>(path[index]);
    escaped[index] = byte >= 0x20 && byte < 0x7f && byte != '\\'
                         ? static_cast<char>(byte)
                         : '?';
  }
  std::fprintf(stderr, "gwipc: listening path=%s role=%u\n", escaped.data(),
               static_cast<unsigned>(role));
}

bool all_zero(const std::uint64_t (&reserved)[4]) noexcept {
  return std::all_of(std::begin(reserved), std::end(reserved),
                     [](const auto value) { return value == 0; });
}

std::uint32_t defaulted(std::uint32_t value, std::uint32_t fallback) noexcept {
  return value == 0 ? fallback : value;
}

std::uint16_t defaulted(std::uint16_t value, std::uint16_t fallback) noexcept {
  return value == 0 ? fallback : value;
}

gwipc_status listener_config(const gwipc_listener_options& options,
                             Config& config) {
  if (options.struct_size < sizeof(options) || !options.path ||
      !all_zero(options.reserved))
    return GWIPC_STATUS_INVALID_ARGUMENT;
  config.path = options.path;
  config.local_role = options.local_role;
  config.peer_roles = options.accepted_peer_roles;
  config.offered_capabilities = options.offered_capabilities;
  config.required_peer_capabilities = options.required_peer_capabilities;
  config.maximum_payload =
      defaulted(options.maximum_payload, GWIPC_DEFAULT_MAXIMUM_PAYLOAD);
  config.maximum_fd_count =
      defaulted(options.maximum_fd_count, GWIPC_DEFAULT_MAXIMUM_FDS);
  config.maximum_queued_bytes = defaulted(
      options.maximum_queued_bytes, GWIPC_DEFAULT_MAXIMUM_QUEUED_BYTES);
  config.maximum_queued_messages = defaulted(
      options.maximum_queued_messages, GWIPC_DEFAULT_MAXIMUM_QUEUED_MESSAGES);
  config.label = options.instance_label ? options.instance_label : "";
  config.require_same_uid = options.allow_any_uid_for_tests == 0;
  if (!fill_instance_id(config.instance_id)) return GWIPC_STATUS_SYSTEM_ERROR;
  return validate_config(config);
}

gwipc_status client_config(const gwipc_connection_options& options,
                           Config& config) {
  if (options.struct_size < sizeof(options) || !options.path ||
      !all_zero(options.reserved))
    return GWIPC_STATUS_INVALID_ARGUMENT;
  config.path = options.path;
  config.local_role = options.local_role;
  config.peer_roles = options.acceptable_server_roles;
  config.offered_capabilities = options.offered_capabilities;
  config.required_peer_capabilities = options.required_peer_capabilities;
  config.maximum_payload =
      defaulted(options.maximum_payload, GWIPC_DEFAULT_MAXIMUM_PAYLOAD);
  config.maximum_fd_count =
      defaulted(options.maximum_fd_count, GWIPC_DEFAULT_MAXIMUM_FDS);
  config.maximum_queued_bytes = defaulted(
      options.maximum_queued_bytes, GWIPC_DEFAULT_MAXIMUM_QUEUED_BYTES);
  config.maximum_queued_messages = defaulted(
      options.maximum_queued_messages, GWIPC_DEFAULT_MAXIMUM_QUEUED_MESSAGES);
  config.label = options.instance_label ? options.instance_label : "";
  if (!fill_instance_id(config.instance_id)) return GWIPC_STATUS_SYSTEM_ERROR;
  return validate_config(config);
}

}  // namespace

bool fill_instance_id(std::array<std::uint8_t, 16>& id) noexcept {
  std::size_t offset = 0;
  while (offset < id.size()) {
    const auto count = ::getrandom(id.data() + offset, id.size() - offset, 0);
    if (count > 0) {
      offset += static_cast<std::size_t>(count);
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    return false;
  }
  if (std::all_of(id.begin(), id.end(), [](auto byte) { return byte == 0; }))
    id[0] = 1;
  return true;
}

bool valid_role(gwipc_role role) noexcept {
  return role >= GWIPC_ROLE_PROTOCOL_SERVER &&
         role <= GWIPC_ROLE_DIAGNOSTIC_TOOL;
}

gwipc_status validate_config(const Config& config) noexcept {
  if (!valid_role(config.local_role) || config.peer_roles == 0 ||
      config.path.empty() || config.label.size() > 64 ||
      config.maximum_payload > GWIPC_HARD_MAXIMUM_PAYLOAD ||
      config.maximum_fd_count > GWIPC_HARD_MAXIMUM_FDS ||
      config.maximum_queued_bytes > GWIPC_HARD_MAXIMUM_QUEUED_BYTES ||
      config.maximum_queued_messages == 0 ||
      (config.required_peer_capabilities & ~kKnownCapabilities) != 0)
    return GWIPC_STATUS_INVALID_ARGUMENT;
  return GWIPC_STATUS_OK;
}

}  // namespace gw::ipc

extern "C" {

gwipc_api_version gwipc_get_api_version(void) { return {0, 8, 0}; }
gwipc_wire_version gwipc_get_max_wire_version(void) { return {1, 0}; }

const char* gwipc_status_string(gwipc_status status) {
  static constexpr const char* names[] = {
      "Ok",          "WouldBlock",       "InProgress",
      "Disconnected", "InvalidArgument",  "InvalidState",
      "OutOfMemory", "LimitExceeded",    "ProtocolError",
      "CredentialRejected", "VersionMismatch", "RoleRejected",
      "CapabilityMismatch", "SystemError"};
  const auto index = static_cast<unsigned>(status);
  return index < std::size(names) ? names[index] : "UnknownStatus";
}

gwipc_status gwipc_listener_create(const gwipc_listener_options* options,
                                   gwipc_listener** out_listener) {
  try {
  if (!options || !out_listener) return GWIPC_STATUS_INVALID_ARGUMENT;
  *out_listener = nullptr;
  auto listener = std::unique_ptr<gwipc_listener>(
      new (std::nothrow) gwipc_listener);
  if (!listener) return GWIPC_STATUS_OUT_OF_MEMORY;
  auto status = gw::ipc::listener_config(*options, listener->config);
  if (status != GWIPC_STATUS_OK) {
    return status;
  }
  gw::ipc::EndpointIdentity identity;
  status = gw::ipc::bind_endpoint(listener->config.path, listener->fd, identity,
                                  listener->system_errno);
  if (status != GWIPC_STATUS_OK) {
    return status;
  }
  listener->path = listener->config.path;
  listener->bound_device = identity.device;
  listener->bound_inode = identity.inode;
  gw::ipc::log_listener_path(listener->path, listener->config.local_role);
  *out_listener = listener.release();
  return GWIPC_STATUS_OK;
  } catch (const std::bad_alloc&) {
    if (out_listener) *out_listener = nullptr;
    return GWIPC_STATUS_OUT_OF_MEMORY;
  } catch (...) {
    if (out_listener) *out_listener = nullptr;
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
}

int gwipc_listener_fd(const gwipc_listener* listener) {
  return listener ? listener->fd : -1;
}

gwipc_status gwipc_listener_accept(gwipc_listener* listener,
                                   gwipc_connection** out_connection) {
  try {
  if (!listener || !out_connection) return GWIPC_STATUS_INVALID_ARGUMENT;
  *out_connection = nullptr;
  const int fd = ::accept4(listener->fd, nullptr, nullptr,
                           SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (fd < 0) {
    listener->system_errno = errno;
    return errno == EAGAIN || errno == EWOULDBLOCK ? GWIPC_STATUS_WOULD_BLOCK
                                                   : GWIPC_STATUS_SYSTEM_ERROR;
  }
  auto connection = std::unique_ptr<gwipc_connection>(
      new (std::nothrow) gwipc_connection);
  if (!connection) {
    (void)::close(fd);
    return GWIPC_STATUS_OUT_OF_MEMORY;
  }
  connection->fd = fd;
  connection->server_side = true;
  connection->state = GWIPC_CONNECTION_AWAITING_HELLO;
  connection->config = listener->config;
  connection->assigned_connection_id = listener->next_connection_id++;
  if (connection->assigned_connection_id == 0) {
    listener->next_connection_id = 0;
    return GWIPC_STATUS_LIMIT_EXCEEDED;
  }
  if (!gw::ipc::read_peer_credentials(fd, connection->peer,
                                      connection->system_errno)) {
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
  connection->peer_credentials_read = true;
  if (connection->config.require_same_uid &&
      connection->peer.uid != static_cast<std::uint32_t>(::geteuid())) {
    return GWIPC_STATUS_CREDENTIAL_REJECTED;
  }
  std::fprintf(stderr, "gwipc: peer accepted pid=%d uid=%u gid=%u\n",
               connection->peer.pid, connection->peer.uid,
               connection->peer.gid);
  *out_connection = connection.release();
  return GWIPC_STATUS_OK;
  } catch (const std::bad_alloc&) {
    if (out_connection) *out_connection = nullptr;
    return GWIPC_STATUS_OUT_OF_MEMORY;
  } catch (...) {
    if (out_connection) *out_connection = nullptr;
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
}

int gwipc_listener_system_errno(const gwipc_listener* listener) {
  return listener ? listener->system_errno : EINVAL;
}

void gwipc_listener_destroy(gwipc_listener* listener) {
  if (!listener) return;
  gw::ipc::cleanup_endpoint(
      listener->path, {listener->bound_device, listener->bound_inode});
  gw::ipc::close_fd(listener->fd);
  delete listener;
}

gwipc_status gwipc_connection_connect(const gwipc_connection_options* options,
                                      gwipc_connection** out_connection) {
  try {
  if (!options || !out_connection) return GWIPC_STATUS_INVALID_ARGUMENT;
  *out_connection = nullptr;
  auto connection = std::unique_ptr<gwipc_connection>(
      new (std::nothrow) gwipc_connection);
  if (!connection) return GWIPC_STATUS_OUT_OF_MEMORY;
  auto status = gw::ipc::client_config(*options, connection->config);
  if (status != GWIPC_STATUS_OK) {
    return status;
  }
  bool in_progress = false;
  status = gw::ipc::connect_endpoint(connection->config.path, connection->fd,
                                     in_progress, connection->system_errno);
  if (status != GWIPC_STATUS_OK && status != GWIPC_STATUS_IN_PROGRESS) {
    return status;
  }
  connection->server_side = false;
  connection->state = in_progress ? GWIPC_CONNECTION_CONNECTING
                                  : GWIPC_CONNECTION_AWAITING_WELCOME;
  if (!in_progress) {
    if (!gw::ipc::read_peer_credentials(connection->fd, connection->peer,
                                        connection->system_errno)) {
      return GWIPC_STATUS_SYSTEM_ERROR;
    }
    connection->peer_credentials_read = true;
    if (connection->config.require_same_uid &&
        connection->peer.uid != static_cast<std::uint32_t>(::geteuid())) {
      return GWIPC_STATUS_CREDENTIAL_REJECTED;
    }
  }
  *out_connection = connection.release();
  return status;
  } catch (const std::bad_alloc&) {
    if (out_connection) *out_connection = nullptr;
    return GWIPC_STATUS_OUT_OF_MEMORY;
  } catch (...) {
    if (out_connection) *out_connection = nullptr;
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
}

int gwipc_connection_fd(const gwipc_connection* connection) {
  return connection ? connection->fd : -1;
}

gwipc_connection_state gwipc_connection_get_state(
    const gwipc_connection* connection) {
  return connection ? connection->state : GWIPC_CONNECTION_CLOSED;
}

gwipc_peer_info gwipc_connection_peer_info(const gwipc_connection* connection) {
  return connection ? connection->peer : gwipc_peer_info{};
}

int gwipc_connection_system_errno(const gwipc_connection* connection) {
  return connection ? connection->system_errno : EINVAL;
}

int gwipc_connection_snapshot_aborted(const gwipc_connection* connection) {
  return connection && connection->snapshot_aborted ? 1 : 0;
}

void gwipc_connection_destroy(gwipc_connection* connection) {
  delete connection;
}

}  // extern "C"
