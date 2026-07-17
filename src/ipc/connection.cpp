#include "ipc/connection_internal.hpp"

#include <poll.h>

#include <cstdint>
#include <new>

extern "C" {

short gwipc_connection_wanted_poll_events(const gwipc_connection* connection) {
  if (!connection || connection->state == GWIPC_CONNECTION_CLOSED) return 0;
  short events = POLLIN;
  if (connection->state == GWIPC_CONNECTION_CONNECTING ||
      (!connection->server_side &&
       connection->state == GWIPC_CONNECTION_AWAITING_WELCOME &&
       connection->next_send_sequence == 1) ||
      !connection->outgoing.empty())
    events |= POLLOUT;
  return events;
}

gwipc_status gwipc_connection_process_poll_events(gwipc_connection* connection,
                                                  short revents) {
  if (!connection) return GWIPC_STATUS_INVALID_ARGUMENT;
  try {
    return gw::ipc::process_poll_events(*connection, revents);
  } catch (const std::bad_alloc&) {
    return GWIPC_STATUS_OUT_OF_MEMORY;
  } catch (...) {
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
}

gwipc_status gwipc_connection_enqueue_with_sequence(
    gwipc_connection* connection, const gwipc_outgoing_message* message,
    std::uint64_t* out_sequence) {
  if (!out_sequence) return GWIPC_STATUS_INVALID_ARGUMENT;
  *out_sequence = 0;
  if (!connection || !message) return GWIPC_STATUS_INVALID_ARGUMENT;
  try {
    return gw::ipc::enqueue_with_sequence(*connection, *message,
                                          *out_sequence);
  } catch (const std::bad_alloc&) {
    return GWIPC_STATUS_OUT_OF_MEMORY;
  } catch (...) {
    return GWIPC_STATUS_SYSTEM_ERROR;
  }
}

gwipc_status gwipc_connection_enqueue(gwipc_connection* connection,
                                      const gwipc_outgoing_message* message) {
  std::uint64_t ignored_sequence = 0;
  return gwipc_connection_enqueue_with_sequence(connection, message,
                                                &ignored_sequence);
}

gwipc_status gwipc_connection_receive(gwipc_connection* connection,
                                      gwipc_message** out_message) {
  if (!connection || !out_message) return GWIPC_STATUS_INVALID_ARGUMENT;
  *out_message = nullptr;
  if (connection->incoming.empty())
    return connection->state == GWIPC_CONNECTION_CLOSED
               ? GWIPC_STATUS_DISCONNECTED
               : GWIPC_STATUS_WOULD_BLOCK;
  *out_message = connection->incoming.front();
  connection->incoming.pop_front();
  return GWIPC_STATUS_OK;
}

}  // extern "C"
