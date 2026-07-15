#ifndef GLASSWYRM_IPC_SESSION_H
#define GLASSWYRM_IPC_SESSION_H

#include <glasswyrm/ipc/contracts.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gwipc_session_state {
  GWIPC_SESSION_INACTIVE = 1,
  GWIPC_SESSION_ACTIVE = 2,
} gwipc_session_state;

typedef struct gwipc_session_state_change {
  size_t struct_size;
  uint64_t generation;
  gwipc_session_state state;
  uint32_t flags;
  uint64_t reserved[4];
} gwipc_session_state_change;

typedef enum gwipc_session_state_result {
  GWIPC_SESSION_STATE_ACCEPTED = 1,
  GWIPC_SESSION_STATE_ALREADY_APPLIED = 2,
  GWIPC_SESSION_STATE_INPUT_UNAVAILABLE = 3,
  GWIPC_SESSION_STATE_FAILED = 4,
} gwipc_session_state_result;

typedef struct gwipc_session_state_acknowledged {
  size_t struct_size;
  uint64_t generation;
  gwipc_session_state state;
  gwipc_session_state_result result;
  uint32_t flags;
  uint64_t reserved[4];
} gwipc_session_state_acknowledged;

#define GWIPC_DECLARE_SESSION_CONTRACT(Type, name) \
  GWIPC_API gwipc_status gwipc_contract_encode_##name( \
      const Type *, gwipc_contract_payload **); \
  GWIPC_API const Type *gwipc_decoded_##name( \
      const gwipc_decoded_contract *)
GWIPC_DECLARE_SESSION_CONTRACT(gwipc_session_state_change,
                               session_state_change);
GWIPC_DECLARE_SESSION_CONTRACT(gwipc_session_state_acknowledged,
                               session_state_acknowledged);
#undef GWIPC_DECLARE_SESSION_CONTRACT

#ifdef __cplusplus
}
#endif

#endif
