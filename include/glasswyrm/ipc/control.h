#ifndef GLASSWYRM_IPC_CONTROL_H
#define GLASSWYRM_IPC_CONTROL_H

#include <glasswyrm/ipc/message.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gwipc_snapshot_domain {
  GWIPC_SNAPSHOT_OUTPUTS = 1,
  GWIPC_SNAPSHOT_SURFACES = 2,
  GWIPC_SNAPSHOT_WINDOW_POLICY = 3,
  GWIPC_SNAPSHOT_COMPLETE_SESSION = 4,
  GWIPC_SNAPSHOT_TEST = 5,
} gwipc_snapshot_domain;

typedef struct gwipc_snapshot_begin {
  size_t struct_size;
  uint64_t snapshot_id;
  gwipc_snapshot_domain domain;
  uint16_t flags;
  uint64_t generation;
  uint32_t expected_item_count;
  uint64_t reserved[4];
} gwipc_snapshot_begin;

typedef struct gwipc_snapshot_end {
  size_t struct_size;
  uint64_t snapshot_id;
  uint64_t generation;
  uint32_t actual_item_count;
  uint64_t reserved[4];
} gwipc_snapshot_end;

typedef struct gwipc_snapshot_abort {
  size_t struct_size;
  uint64_t snapshot_id;
  uint16_t reason;
  const char *detail;
  size_t detail_length;
  uint64_t reserved[4];
} gwipc_snapshot_abort;

typedef struct gwipc_control_payload gwipc_control_payload;
typedef struct gwipc_decoded_control gwipc_decoded_control;

GWIPC_API gwipc_status gwipc_control_encode_snapshot_begin(
    const gwipc_snapshot_begin *value, gwipc_control_payload **out_payload);
GWIPC_API gwipc_status gwipc_control_encode_snapshot_end(
    const gwipc_snapshot_end *value, gwipc_control_payload **out_payload);
GWIPC_API gwipc_status gwipc_control_encode_snapshot_abort(
    const gwipc_snapshot_abort *value, gwipc_control_payload **out_payload);
GWIPC_API const uint8_t *gwipc_control_payload_data(
    const gwipc_control_payload *payload, size_t *out_size);
GWIPC_API void gwipc_control_payload_destroy(gwipc_control_payload *payload);

GWIPC_API gwipc_status gwipc_control_decode_message(
    const gwipc_message *message, gwipc_decoded_control **out_control);
GWIPC_API uint16_t gwipc_decoded_control_type(
    const gwipc_decoded_control *control);
GWIPC_API const gwipc_snapshot_begin *gwipc_decoded_snapshot_begin(
    const gwipc_decoded_control *control);
GWIPC_API const gwipc_snapshot_end *gwipc_decoded_snapshot_end(
    const gwipc_decoded_control *control);
GWIPC_API const gwipc_snapshot_abort *gwipc_decoded_snapshot_abort(
    const gwipc_decoded_control *control);
GWIPC_API void gwipc_decoded_control_destroy(gwipc_decoded_control *control);

#ifdef __cplusplus
}
#endif

#endif
