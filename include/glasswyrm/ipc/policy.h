#ifndef GLASSWYRM_IPC_POLICY_H
#define GLASSWYRM_IPC_POLICY_H

#include <glasswyrm/ipc/contracts.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gwipc_policy_window_type {
  GWIPC_POLICY_WINDOW_UNKNOWN = 0,
  GWIPC_POLICY_WINDOW_NORMAL = 1,
  GWIPC_POLICY_WINDOW_DIALOG = 2,
  GWIPC_POLICY_WINDOW_UTILITY = 3,
} gwipc_policy_window_type;
typedef enum gwipc_policy_map_intent {
  GWIPC_POLICY_UNMAPPED = 0,
  GWIPC_POLICY_WANTS_MAP = 1,
} gwipc_policy_map_intent;
typedef enum gwipc_policy_applied_state {
  GWIPC_POLICY_APPLIED_NORMAL = 1,
  GWIPC_POLICY_APPLIED_MAXIMIZED = 2,
  GWIPC_POLICY_APPLIED_FULLSCREEN = 3,
  GWIPC_POLICY_APPLIED_MINIMIZED = 4,
} gwipc_policy_applied_state;
typedef enum gwipc_policy_result {
  GWIPC_POLICY_ACCEPTED = 1,
  GWIPC_POLICY_REJECTED_INCOMPLETE_SNAPSHOT = 2,
  GWIPC_POLICY_REJECTED_INVALID_CONTEXT = 3,
  GWIPC_POLICY_REJECTED_INVALID_WINDOW = 4,
  GWIPC_POLICY_REJECTED_UNKNOWN_REFERENCE = 5,
  GWIPC_POLICY_REJECTED_LIMIT = 6,
  GWIPC_POLICY_REJECTED_UNSUPPORTED_METADATA = 7,
} gwipc_policy_result;
enum {
  GWIPC_POLICY_WINDOW_FLAG_ABOVE = UINT32_C(1) << 0,
  GWIPC_POLICY_WINDOW_FLAG_BYPASS_COMPOSITOR = UINT32_C(1) << 1,
  GWIPC_POLICY_WINDOW_FLAG_INPUT_DISABLED = UINT32_C(1) << 2,
};

typedef struct gwipc_policy_context_upsert {
  size_t struct_size; uint32_t root_window_id, workspace_id; uint64_t output_id;
  int32_t work_x, work_y; uint32_t work_width, work_height, flags; uint64_t reserved[4];
} gwipc_policy_context_upsert;
typedef struct gwipc_policy_window_upsert {
  size_t struct_size; uint32_t window_id, parent_window_id, transient_for, workspace_id;
  int32_t requested_x, requested_y; uint32_t requested_width, requested_height, border_width;
  gwipc_policy_window_type window_type; gwipc_policy_map_intent map_intent;
  uint8_t override_redirect; gwipc_tri_state decoration_preference;
  uint8_t fullscreen_requested, maximized_requested, minimized_requested, attention_requested;
  uint64_t creation_serial, map_serial, focus_serial; uint32_t flags; uint64_t reserved[4];
} gwipc_policy_window_upsert;
typedef struct gwipc_policy_window_remove { size_t struct_size; uint32_t window_id; uint64_t reserved[4]; } gwipc_policy_window_remove;
typedef struct gwipc_policy_commit { size_t struct_size; uint64_t commit_id, producer_generation; uint32_t flags; uint64_t reserved[4]; } gwipc_policy_commit;
typedef struct gwipc_policy_window_state {
  size_t struct_size; uint32_t window_id, transient_for, workspace_id; uint64_t output_id;
  int32_t final_x, final_y; uint32_t final_width, final_height; int32_t stacking;
  gwipc_policy_window_type window_type; gwipc_policy_applied_state applied_state;
  uint8_t visible, focused, managed, decoration_eligible, override_redirect, attention_requested;
  gwipc_tri_state fullscreen_eligible, direct_scanout_eligible; uint32_t flags; uint64_t reserved[4];
} gwipc_policy_window_state;
typedef struct gwipc_policy_acknowledged {
  size_t struct_size; uint64_t commit_id, producer_generation, applied_generation, policy_hash;
  uint32_t window_count; gwipc_policy_result result; uint64_t reserved[4];
} gwipc_policy_acknowledged;
typedef struct gwipc_policy_bindings_upsert {
  size_t struct_size;
  uint16_t move_modifiers;
  uint16_t resize_modifiers;
  uint16_t close_modifiers;
  uint16_t reserved16;
  uint8_t move_button;
  uint8_t resize_button;
  uint16_t reserved_buttons;
  uint32_t close_keysym;
  uint32_t minimum_width;
  uint32_t minimum_height;
  uint8_t raise_on_focus;
  uint8_t consume_wm_bindings;
  uint16_t reserved_flags;
  uint64_t reserved[4];
} gwipc_policy_bindings_upsert;

#define GWIPC_DECLARE_POLICY_CONTRACT(Type, name) \
  GWIPC_API gwipc_status gwipc_contract_encode_##name(const Type *, gwipc_contract_payload **); \
  GWIPC_API const Type *gwipc_decoded_##name(const gwipc_decoded_contract *)
GWIPC_DECLARE_POLICY_CONTRACT(gwipc_policy_context_upsert, policy_context_upsert);
GWIPC_DECLARE_POLICY_CONTRACT(gwipc_policy_window_upsert, policy_window_upsert);
GWIPC_DECLARE_POLICY_CONTRACT(gwipc_policy_window_remove, policy_window_remove);
GWIPC_DECLARE_POLICY_CONTRACT(gwipc_policy_commit, policy_commit);
GWIPC_DECLARE_POLICY_CONTRACT(gwipc_policy_window_state, policy_window_state);
GWIPC_DECLARE_POLICY_CONTRACT(gwipc_policy_acknowledged, policy_acknowledged);
GWIPC_DECLARE_POLICY_CONTRACT(gwipc_policy_bindings_upsert, policy_bindings_upsert);
#undef GWIPC_DECLARE_POLICY_CONTRACT

#ifdef __cplusplus
}
#endif
#endif
