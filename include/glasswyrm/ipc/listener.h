#ifndef GLASSWYRM_IPC_LISTENER_H
#define GLASSWYRM_IPC_LISTENER_H

#include <glasswyrm/ipc/types.h>

#ifdef __cplusplus
extern "C" {
#endif

GWIPC_API gwipc_status gwipc_listener_create(
    const gwipc_listener_options *options, gwipc_listener **out_listener);
GWIPC_API int gwipc_listener_fd(const gwipc_listener *listener);
GWIPC_API gwipc_status gwipc_listener_accept(
    gwipc_listener *listener, gwipc_connection **out_connection);
GWIPC_API int gwipc_listener_system_errno(const gwipc_listener *listener);
GWIPC_API void gwipc_listener_destroy(gwipc_listener *listener);

#ifdef __cplusplus
}
#endif

#endif
