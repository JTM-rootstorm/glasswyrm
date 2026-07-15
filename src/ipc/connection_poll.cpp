#include "ipc/connection_internal.hpp"

#include "ipc/endpoint.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

namespace gw::ipc {
namespace {

gwipc_status finish_connect(gwipc_connection& connection) {
  int error = 0;
  socklen_t size = sizeof(error);
  if (::getsockopt(connection.fd, SOL_SOCKET, SO_ERROR, &error, &size) < 0 ||
      error != 0) {
    connection.system_errno = error != 0 ? error : errno;
    set_closed(connection);
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
  connection.state = GWIPC_CONNECTION_AWAITING_WELCOME;
  if (!read_peer_credentials(connection.fd, connection.peer,
                             connection.system_errno)) {
    set_closed(connection);
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
  connection.peer_credentials_read = true;
  if (connection.config.require_same_uid &&
      connection.peer.uid != static_cast<std::uint32_t>(::geteuid())) {
    set_closed(connection);
    return GWIPC_STATUS_CREDENTIAL_REJECTED;
  }
  return GWIPC_STATUS_OK;
}

gwipc_status drain_incoming(gwipc_connection& connection) {
  gwipc_status result = GWIPC_STATUS_OK;
  for (;;) {
    const auto status = receive_one(connection);
    if (status == GWIPC_STATUS_WOULD_BLOCK) break;
    if (status != GWIPC_STATUS_OK) {
      result = status;
      if (status == GWIPC_STATUS_PROTOCOL_ERROR ||
          status == GWIPC_STATUS_LIMIT_EXCEEDED ||
          status == GWIPC_STATUS_CAPABILITY_MISMATCH)
        std::fprintf(stderr,
                     "gwipc: protocol failure status=%s connection=%llu\n",
                     gwipc_status_string(status),
                     static_cast<unsigned long long>(
                         connection.peer.connection_id));
    }
    if (connection.state == GWIPC_CONNECTION_CLOSED ||
        status == GWIPC_STATUS_DISCONNECTED ||
        status == GWIPC_STATUS_SYSTEM_ERROR ||
        status == GWIPC_STATUS_PROTOCOL_ERROR)
      break;
  }
  return result;
}

}  // namespace

gwipc_status process_poll_events(gwipc_connection& connection,
                                 short revents) {
  if (connection.state == GWIPC_CONNECTION_CLOSED)
    return GWIPC_STATUS_DISCONNECTED;
  if (connection.state == GWIPC_CONNECTION_CONNECTING &&
      (revents & POLLOUT) != 0) {
    const auto status = finish_connect(connection);
    if (status != GWIPC_STATUS_OK) return status;
  }
  if (!connection.server_side &&
      connection.state == GWIPC_CONNECTION_AWAITING_WELCOME &&
      connection.next_send_sequence == 1) {
    const auto status = queue_hello(connection);
    if (status != GWIPC_STATUS_OK) return status;
  }
  auto result = GWIPC_STATUS_OK;
  if ((revents & POLLIN) != 0) result = drain_incoming(connection);
  if ((revents & (POLLERR | POLLNVAL)) != 0) {
    set_closed(connection);
    return GWIPC_STATUS_DISCONNECTED;
  }
  if (!connection.outgoing.empty() &&
      ((revents & POLLOUT) != 0 ||
       connection.state == GWIPC_CONNECTION_REJECTING)) {
    const auto status = flush(connection);
    if (status != GWIPC_STATUS_OK && status != GWIPC_STATUS_WOULD_BLOCK)
      return status;
  }
  if ((revents & POLLHUP) != 0 && connection.outgoing.empty()) {
    set_closed(connection);
    return result == GWIPC_STATUS_OK ? GWIPC_STATUS_DISCONNECTED : result;
  }
  return result;
}

}  // namespace gw::ipc
