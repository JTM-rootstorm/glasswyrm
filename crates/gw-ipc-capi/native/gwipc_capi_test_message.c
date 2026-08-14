#include <glasswyrm/ipc/message.h>

/*
 * Test-only stand-in for the native message object. The bridge does not own or
 * replace message transport; this file merely lets the public decode entry
 * point be exercised without linking native libgwipc into the smoke library.
 */
struct gwipc_message {
  uint16_t type;
  const uint8_t *payload;
  size_t payload_size;
};

uint16_t gwipc_message_type(const gwipc_message *message) {
  return message != NULL ? message->type : 0;
}

const uint8_t *gwipc_message_payload(const gwipc_message *message,
                                     size_t *out_size) {
  if (out_size != NULL)
    *out_size = message != NULL ? message->payload_size : 0;
  return message != NULL ? message->payload : NULL;
}
