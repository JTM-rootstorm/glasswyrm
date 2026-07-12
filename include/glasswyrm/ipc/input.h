#ifndef GLASSWYRM_IPC_INPUT_H
#define GLASSWYRM_IPC_INPUT_H

#include <glasswyrm/ipc/contracts.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gwipc_synthetic_input_result {
  GWIPC_SYNTHETIC_INPUT_ACCEPTED = 1,
  GWIPC_SYNTHETIC_INPUT_CLAMPED = 2,
  GWIPC_SYNTHETIC_INPUT_INVALID_TRANSITION = 3,
  GWIPC_SYNTHETIC_INPUT_FOCUS_UNCHANGED = 4,
  GWIPC_SYNTHETIC_INPUT_FOCUS_REJECTED = 5,
  GWIPC_SYNTHETIC_INPUT_LIMIT_EXCEEDED = 6,
} gwipc_synthetic_input_result;

typedef struct gwipc_synthetic_motion {
  size_t struct_size; uint64_t input_id; uint32_t time_ms;
  int32_t root_x, root_y; uint32_t flags; uint64_t reserved[4];
} gwipc_synthetic_motion;
typedef struct gwipc_synthetic_button {
  size_t struct_size; uint64_t input_id; uint32_t time_ms;
  uint8_t button, pressed; uint16_t reserved16; uint32_t flags;
  uint64_t reserved[4];
} gwipc_synthetic_button;
typedef struct gwipc_synthetic_key {
  size_t struct_size; uint64_t input_id; uint32_t time_ms;
  uint8_t keycode, pressed; uint16_t reserved16; uint32_t flags;
  uint64_t reserved[4];
} gwipc_synthetic_key;
typedef struct gwipc_synthetic_barrier {
  size_t struct_size; uint64_t input_id; uint32_t flags; uint64_t reserved[4];
} gwipc_synthetic_barrier;
typedef struct gwipc_synthetic_input_acknowledged {
  size_t struct_size; uint64_t input_id; uint32_t time_ms;
  gwipc_synthetic_input_result result; int32_t root_x, root_y;
  uint32_t pointer_window, focus_window; uint16_t state, reserved16;
  uint32_t delivered_event_count, flags; uint64_t reserved[4];
} gwipc_synthetic_input_acknowledged;

#define GWIPC_DECLARE_INPUT_CONTRACT(Type, name) \
  GWIPC_API gwipc_status gwipc_contract_encode_##name(const Type *, gwipc_contract_payload **); \
  GWIPC_API const Type *gwipc_decoded_##name(const gwipc_decoded_contract *)
GWIPC_DECLARE_INPUT_CONTRACT(gwipc_synthetic_motion, synthetic_motion);
GWIPC_DECLARE_INPUT_CONTRACT(gwipc_synthetic_button, synthetic_button);
GWIPC_DECLARE_INPUT_CONTRACT(gwipc_synthetic_key, synthetic_key);
GWIPC_DECLARE_INPUT_CONTRACT(gwipc_synthetic_barrier, synthetic_barrier);
GWIPC_DECLARE_INPUT_CONTRACT(gwipc_synthetic_input_acknowledged, synthetic_input_acknowledged);
#undef GWIPC_DECLARE_INPUT_CONTRACT

#ifdef __cplusplus
}
#endif
#endif
