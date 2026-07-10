#ifndef GLASSWYRM_IPC_CONNECTION_H
#define GLASSWYRM_IPC_CONNECTION_H

#include <glasswyrm/ipc/types.h>

#ifdef __cplusplus
extern "C" {
#endif

GWIPC_API gwipc_status gwipc_connection_connect(
    const gwipc_connection_options *options, gwipc_connection **out_connection);
GWIPC_API int gwipc_connection_fd(const gwipc_connection *connection);
GWIPC_API short gwipc_connection_wanted_poll_events(
    const gwipc_connection *connection);
GWIPC_API gwipc_status gwipc_connection_process_poll_events(
    gwipc_connection *connection, short revents);
GWIPC_API gwipc_connection_state gwipc_connection_get_state(
    const gwipc_connection *connection);
GWIPC_API gwipc_status gwipc_connection_enqueue(
    gwipc_connection *connection, const gwipc_outgoing_message *message);
GWIPC_API gwipc_status gwipc_connection_receive(
    gwipc_connection *connection, gwipc_message **out_message);
GWIPC_API gwipc_peer_info gwipc_connection_peer_info(
    const gwipc_connection *connection);
GWIPC_API int gwipc_connection_system_errno(
    const gwipc_connection *connection);
GWIPC_API int gwipc_connection_snapshot_aborted(
    const gwipc_connection *connection);
GWIPC_API void gwipc_connection_destroy(gwipc_connection *connection);

#ifdef __cplusplus
}
#endif

#endif
