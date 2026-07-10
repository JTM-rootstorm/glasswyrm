#ifndef GLASSWYRM_IPC_VERSION_H
#define GLASSWYRM_IPC_VERSION_H

#include <glasswyrm/ipc/types.h>

#ifdef __cplusplus
extern "C" {
#endif

GWIPC_API gwipc_api_version gwipc_get_api_version(void);
GWIPC_API gwipc_wire_version gwipc_get_max_wire_version(void);
GWIPC_API const char *gwipc_status_string(gwipc_status status);

#ifdef __cplusplus
}
#endif

#endif
