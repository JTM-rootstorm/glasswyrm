#ifndef GLASSWYRM_IPC_VRR_H
#define GLASSWYRM_IPC_VRR_H

#include <glasswyrm/ipc/contracts.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gwipc_vrr_policy_mode {
  GWIPC_VRR_POLICY_OFF = 1,
  GWIPC_VRR_POLICY_FULLSCREEN = 2,
  GWIPC_VRR_POLICY_FOCUSED = 3,
  GWIPC_VRR_POLICY_APP_REQUESTED = 4,
  GWIPC_VRR_POLICY_ALWAYS_ELIGIBLE = 5,
} gwipc_vrr_policy_mode;

typedef enum gwipc_vrr_window_preference {
  GWIPC_VRR_PREFERENCE_DEFAULT = 0,
  GWIPC_VRR_PREFERENCE_DISABLE = 1,
  GWIPC_VRR_PREFERENCE_ALLOW = 2,
  GWIPC_VRR_PREFERENCE_PREFER = 3,
} gwipc_vrr_window_preference;

typedef enum gwipc_vrr_decision {
  GWIPC_VRR_DECISION_DISABLED = 1,
  GWIPC_VRR_DECISION_ENABLED = 2,
  GWIPC_VRR_DECISION_UNSUPPORTED = 3,
  GWIPC_VRR_DECISION_REJECTED = 4,
} gwipc_vrr_decision;

#define GWIPC_VRR_REASON_OUTPUT_DISABLED (UINT64_C(1) << 0)
#define GWIPC_VRR_REASON_OUTPUT_NOT_CONNECTED (UINT64_C(1) << 1)
#define GWIPC_VRR_REASON_OUTPUT_NOT_DRM (UINT64_C(1) << 2)
#define GWIPC_VRR_REASON_OUTPUT_NOT_VRR_CAPABLE (UINT64_C(1) << 3)
#define GWIPC_VRR_REASON_ATOMIC_KMS_UNAVAILABLE (UINT64_C(1) << 4)
#define GWIPC_VRR_REASON_VRR_PROPERTY_MISSING (UINT64_C(1) << 5)
#define GWIPC_VRR_REASON_VRR_ATOMIC_TEST_FAILED (UINT64_C(1) << 6)
#define GWIPC_VRR_REASON_SESSION_INACTIVE (UINT64_C(1) << 7)
#define GWIPC_VRR_REASON_VT_SUSPENDED (UINT64_C(1) << 8)
#define GWIPC_VRR_REASON_OUTPUT_CONFIGURATION_BUSY (UINT64_C(1) << 9)
#define GWIPC_VRR_REASON_POLICY_OFF (UINT64_C(1) << 10)
#define GWIPC_VRR_REASON_NO_CANDIDATE (UINT64_C(1) << 11)
#define GWIPC_VRR_REASON_WINDOW_MISSING (UINT64_C(1) << 12)
#define GWIPC_VRR_REASON_WINDOW_HIDDEN (UINT64_C(1) << 13)
#define GWIPC_VRR_REASON_WINDOW_UNMANAGED (UINT64_C(1) << 14)
#define GWIPC_VRR_REASON_WINDOW_UNFOCUSED (UINT64_C(1) << 15)
#define GWIPC_VRR_REASON_WINDOW_NOT_FULLSCREEN (UINT64_C(1) << 16)
#define GWIPC_VRR_REASON_WINDOW_NOT_BORDERLESS_FULLSCREEN (UINT64_C(1) << 17)
#define GWIPC_VRR_REASON_WINDOW_SPANS_OUTPUTS (UINT64_C(1) << 18)
#define GWIPC_VRR_REASON_WINDOW_PREFERENCE_DISABLED (UINT64_C(1) << 19)
#define GWIPC_VRR_REASON_WINDOW_DID_NOT_REQUEST (UINT64_C(1) << 20)
#define GWIPC_VRR_REASON_SURFACE_MISSING (UINT64_C(1) << 21)
#define GWIPC_VRR_REASON_SURFACE_METADATA_ONLY (UINT64_C(1) << 22)
#define GWIPC_VRR_REASON_SURFACE_NOT_VISIBLE (UINT64_C(1) << 23)
#define GWIPC_VRR_REASON_SURFACE_NOT_OPAQUE (UINT64_C(1) << 24)
#define GWIPC_VRR_REASON_SURFACE_ON_WRONG_OUTPUT (UINT64_C(1) << 25)
#define GWIPC_VRR_REASON_SURFACE_MEMBERSHIP_INVALID (UINT64_C(1) << 26)
#define GWIPC_VRR_REASON_PRESENTER_REJECTED (UINT64_C(1) << 27)
#define GWIPC_VRR_REASON_PROPERTY_READBACK_MISMATCH (UINT64_C(1) << 28)
#define GWIPC_VRR_REASON_TIMING_UNAVAILABLE (UINT64_C(1) << 29)
#define GWIPC_VRR_REASON_HARDWARE_BEHAVIOR_UNCONFIRMED (UINT64_C(1) << 30)
#define GWIPC_VRR_REASON_SIMULATED_HEADLESS (UINT64_C(1) << 31)
#define GWIPC_VRR_REASON_MANUAL_ALWAYS_ELIGIBLE (UINT64_C(1) << 32)
#define GWIPC_VRR_KNOWN_REASON_MASK ((UINT64_C(1) << 33) - UINT64_C(1))

enum {
  GWIPC_PRESENTATION_TIMING_SIMULATED = UINT32_C(1) << 0,
};

typedef struct gwipc_output_vrr_capability_upsert {
  size_t struct_size;
  uint64_t output_id;
  uint8_t connector_property_present;
  uint8_t hardware_capable;
  uint8_t kms_controllable;
  uint8_t simulated;
  uint8_t range_available;
  uint8_t atomic_required;
  uint16_t reserved16;
  uint32_t minimum_refresh_millihertz;
  uint32_t maximum_refresh_millihertz;
  uint64_t reason_flags;
  uint32_t flags;
  uint64_t reserved[4];
} gwipc_output_vrr_capability_upsert;

typedef struct gwipc_output_vrr_policy_upsert {
  size_t struct_size;
  uint64_t output_id;
  gwipc_vrr_policy_mode mode;
  uint32_t flags;
  uint64_t reserved[4];
} gwipc_output_vrr_policy_upsert;

typedef struct gwipc_output_vrr_state_upsert {
  size_t struct_size;
  uint64_t output_id;
  gwipc_vrr_policy_mode requested_mode;
  gwipc_vrr_decision decision;
  uint8_t desired_enabled;
  uint8_t effective_enabled;
  uint8_t property_readback_valid;
  uint8_t session_active;
  uint32_t candidate_window_id;
  uint64_t candidate_surface_id;
  uint64_t reason_flags;
  uint64_t state_generation;
  uint64_t transition_serial;
  uint64_t last_commit_id;
  uint64_t last_presented_generation;
  uint32_t last_flip_sequence;
  uint32_t flags;
  uint64_t last_flip_timestamp_nanoseconds;
  uint64_t last_interval_nanoseconds;
  uint64_t reserved[4];
} gwipc_output_vrr_state_upsert;

typedef struct gwipc_surface_vrr_state {
  size_t struct_size;
  uint64_t surface_id;
  uint32_t window_id;
  uint64_t output_id;
  gwipc_vrr_window_preference preference;
  uint8_t policy_selected;
  uint8_t policy_eligible;
  uint8_t focused;
  uint8_t fullscreen;
  uint8_t borderless_fullscreen;
  uint8_t exclusive_output_membership;
  uint16_t reserved16;
  uint64_t reason_flags;
  uint64_t policy_generation;
  uint32_t flags;
  uint64_t reserved[4];
} gwipc_surface_vrr_state;

typedef struct gwipc_policy_window_vrr_upsert {
  size_t struct_size;
  uint32_t window_id;
  gwipc_vrr_window_preference preference;
  uint32_t flags;
  uint64_t reserved[4];
} gwipc_policy_window_vrr_upsert;

typedef struct gwipc_policy_output_vrr_upsert {
  size_t struct_size;
  uint64_t output_id;
  gwipc_vrr_policy_mode mode;
  uint8_t hardware_capable;
  uint8_t kms_controllable;
  uint16_t reserved16;
  uint32_t flags;
  uint64_t reserved[4];
} gwipc_policy_output_vrr_upsert;

typedef struct gwipc_policy_window_vrr_state {
  size_t struct_size;
  uint32_t window_id;
  uint64_t output_id;
  gwipc_vrr_window_preference preference;
  uint8_t selected;
  uint8_t eligible;
  uint8_t focused;
  uint8_t fullscreen;
  uint8_t borderless_fullscreen;
  uint8_t exclusive_output_membership;
  uint16_t reserved16;
  uint64_t reason_flags;
  uint32_t flags;
  uint64_t reserved[4];
} gwipc_policy_window_vrr_state;

typedef struct gwipc_policy_output_vrr_state {
  size_t struct_size;
  uint64_t output_id;
  gwipc_vrr_policy_mode mode;
  uint32_t selected_window_id;
  uint8_t desired_enabled;
  uint8_t candidate_required;
  uint16_t reserved16;
  uint64_t reason_flags;
  uint32_t flags;
  uint64_t reserved[4];
} gwipc_policy_output_vrr_state;

typedef struct gwipc_presentation_timing {
  size_t struct_size;
  uint64_t output_id;
  uint64_t commit_id;
  uint64_t presented_generation;
  uint32_t flip_sequence;
  uint32_t flags;
  uint64_t kernel_timestamp_nanoseconds;
  uint64_t interval_nanoseconds;
  uint8_t effective_vrr_enabled;
  uint8_t timestamp_available;
  uint16_t reserved16;
  uint64_t reserved[4];
} gwipc_presentation_timing;

#define GWIPC_DECLARE_VRR_CONTRACT(Type, name)                                \
  GWIPC_API gwipc_status gwipc_contract_encode_##name(                        \
      const Type *, gwipc_contract_payload **);                               \
  GWIPC_API const Type *gwipc_decoded_##name(                                 \
      const gwipc_decoded_contract *)

GWIPC_DECLARE_VRR_CONTRACT(gwipc_output_vrr_capability_upsert,
                           output_vrr_capability_upsert);
GWIPC_DECLARE_VRR_CONTRACT(gwipc_output_vrr_policy_upsert,
                           output_vrr_policy_upsert);
GWIPC_DECLARE_VRR_CONTRACT(gwipc_output_vrr_state_upsert,
                           output_vrr_state_upsert);
GWIPC_DECLARE_VRR_CONTRACT(gwipc_surface_vrr_state, surface_vrr_state);
GWIPC_DECLARE_VRR_CONTRACT(gwipc_policy_window_vrr_upsert,
                           policy_window_vrr_upsert);
GWIPC_DECLARE_VRR_CONTRACT(gwipc_policy_output_vrr_upsert,
                           policy_output_vrr_upsert);
GWIPC_DECLARE_VRR_CONTRACT(gwipc_policy_window_vrr_state,
                           policy_window_vrr_state);
GWIPC_DECLARE_VRR_CONTRACT(gwipc_policy_output_vrr_state,
                           policy_output_vrr_state);
GWIPC_DECLARE_VRR_CONTRACT(gwipc_presentation_timing, presentation_timing);

#undef GWIPC_DECLARE_VRR_CONTRACT

#ifdef __cplusplus
}
#endif

#endif
