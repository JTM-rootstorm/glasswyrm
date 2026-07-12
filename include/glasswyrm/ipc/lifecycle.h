#ifndef GLASSWYRM_IPC_LIFECYCLE_H
#define GLASSWYRM_IPC_LIFECYCLE_H

#include <glasswyrm/ipc/policy.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gwipc_policy_stack_mode {
  GWIPC_POLICY_STACK_NONE = 0,
  GWIPC_POLICY_STACK_ABOVE = 1,
  GWIPC_POLICY_STACK_BELOW = 2,
} gwipc_policy_stack_mode;

typedef struct gwipc_policy_lifecycle_window_upsert {
  size_t struct_size;
  gwipc_policy_window_upsert window;
  uint64_t geometry_serial;
  uint64_t stack_serial;
  uint32_t stack_sibling;
  gwipc_policy_stack_mode stack_mode;
  uint32_t flags;
  uint64_t reserved[4];
} gwipc_policy_lifecycle_window_upsert;

typedef struct gwipc_surface_policy_upsert {
  size_t struct_size;
  uint64_t surface_id;
  uint32_t x11_window_id;
  uint32_t workspace_id;
  gwipc_policy_window_type window_type;
  gwipc_policy_applied_state applied_state;
  uint8_t focused;
  uint8_t managed;
  uint8_t decoration_eligible;
  uint8_t override_redirect;
  uint8_t attention_requested;
  gwipc_tri_state fullscreen_eligible;
  gwipc_tri_state direct_scanout_eligible;
  uint32_t flags;
  uint64_t reserved[4];
} gwipc_surface_policy_upsert;

#define GWIPC_DECLARE_LIFECYCLE_CONTRACT(Type, name)                         \
  GWIPC_API gwipc_status gwipc_contract_encode_##name(                       \
      const Type *, gwipc_contract_payload **);                              \
  GWIPC_API const Type *gwipc_decoded_##name(                                \
      const gwipc_decoded_contract *)
GWIPC_DECLARE_LIFECYCLE_CONTRACT(gwipc_policy_lifecycle_window_upsert,
                                 policy_lifecycle_window_upsert);
GWIPC_DECLARE_LIFECYCLE_CONTRACT(gwipc_surface_policy_upsert,
                                 surface_policy_upsert);
#undef GWIPC_DECLARE_LIFECYCLE_CONTRACT

#ifdef __cplusplus
}
#endif
#endif
