#ifndef GLASSWYRM_IPC_MESSAGE_H
#define GLASSWYRM_IPC_MESSAGE_H

#include <glasswyrm/ipc/types.h>

#ifdef __cplusplus
extern "C" {
#endif

GWIPC_API uint16_t gwipc_message_type(const gwipc_message *message);
GWIPC_API uint32_t gwipc_message_flags(const gwipc_message *message);
GWIPC_API uint64_t gwipc_message_sequence(const gwipc_message *message);
GWIPC_API uint64_t gwipc_message_reply_to(const gwipc_message *message);
GWIPC_API const uint8_t *gwipc_message_payload(
    const gwipc_message *message, size_t *out_size);
GWIPC_API size_t gwipc_message_fd_count(const gwipc_message *message);
GWIPC_API gwipc_status gwipc_message_take_fd(
    gwipc_message *message, size_t index, int *out_fd);
GWIPC_API void gwipc_message_destroy(gwipc_message *message);

#ifdef __cplusplus
}
#endif

#endif
