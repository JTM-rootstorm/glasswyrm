#ifndef GLASSWYRM_IPC_OUTPUT_H
#define GLASSWYRM_IPC_OUTPUT_H

#include <glasswyrm/ipc/contracts.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GWIPC_MAXIMUM_OUTPUTS 8U
#define GWIPC_MAXIMUM_OUTPUT_NAME_BYTES 63U
#define GWIPC_MAXIMUM_OUTPUT_MODES 128U
#define GWIPC_MAXIMUM_OUTPUT_PHYSICAL_EXTENT 4096U
#define GWIPC_MAXIMUM_OUTPUT_PHYSICAL_PIXELS UINT64_C(16777216)
#define GWIPC_MAXIMUM_TOTAL_OUTPUT_PIXELS UINT64_C(67108864)
#define GWIPC_MAXIMUM_ROOT_LOGICAL_WIDTH 32767U
#define GWIPC_MAXIMUM_ROOT_LOGICAL_HEIGHT 32767U
#define GWIPC_MINIMUM_OUTPUT_SCALE_NUMERATOR 1U
#define GWIPC_MAXIMUM_OUTPUT_SCALE_NUMERATOR 4U
#define GWIPC_MAXIMUM_OUTPUT_SCALE_DENOMINATOR 120U
#define GWIPC_MAXIMUM_OUTPUT_CONTROL_PEERS 8U

typedef enum gwipc_output_kind {
  GWIPC_OUTPUT_HEADLESS = 1,
  GWIPC_OUTPUT_DRM = 2,
} gwipc_output_kind;

enum {
  GWIPC_OUTPUT_CAP_CONNECTED = UINT32_C(1) << 0,
  GWIPC_OUTPUT_CAP_ARBITRARY_HEADLESS_MODE = UINT32_C(1) << 1,
  GWIPC_OUTPUT_CAP_MODE_FIXED = UINT32_C(1) << 2,
  GWIPC_OUTPUT_CAP_SCALE_CONFIGURABLE = UINT32_C(1) << 3,
  GWIPC_OUTPUT_CAP_TRANSFORM_CONFIGURABLE = UINT32_C(1) << 4,
  GWIPC_OUTPUT_CAP_PRIMARY_ELIGIBLE = UINT32_C(1) << 5,
  GWIPC_OUTPUT_CAP_PHYSICAL_DIMENSIONS_KNOWN = UINT32_C(1) << 6,
};

enum {
  GWIPC_OUTPUT_TRANSFORM_NORMAL = UINT32_C(1) << GWIPC_TRANSFORM_NORMAL,
  GWIPC_OUTPUT_TRANSFORM_ROTATE_90 = UINT32_C(1) << GWIPC_TRANSFORM_ROTATE_90,
  GWIPC_OUTPUT_TRANSFORM_ROTATE_180 = UINT32_C(1) << GWIPC_TRANSFORM_ROTATE_180,
  GWIPC_OUTPUT_TRANSFORM_ROTATE_270 = UINT32_C(1) << GWIPC_TRANSFORM_ROTATE_270,
  GWIPC_OUTPUT_TRANSFORM_FLIPPED = UINT32_C(1) << GWIPC_TRANSFORM_FLIPPED,
  GWIPC_OUTPUT_TRANSFORM_FLIPPED_90 = UINT32_C(1) << GWIPC_TRANSFORM_FLIPPED_90,
  GWIPC_OUTPUT_TRANSFORM_FLIPPED_180 = UINT32_C(1)
                                       << GWIPC_TRANSFORM_FLIPPED_180,
  GWIPC_OUTPUT_TRANSFORM_FLIPPED_270 = UINT32_C(1)
                                       << GWIPC_TRANSFORM_FLIPPED_270,
};

typedef struct gwipc_output_descriptor_upsert {
  size_t struct_size;
  uint64_t output_id;
  gwipc_output_kind kind;
  uint32_t capability_flags;
  const char *name;
  size_t name_length;
  uint32_t physical_width_millimeters;
  uint32_t physical_height_millimeters;
  uint32_t supported_transform_mask;
  uint32_t minimum_scale_numerator;
  uint32_t minimum_scale_denominator;
  uint32_t maximum_scale_numerator;
  uint32_t maximum_scale_denominator;
  uint32_t maximum_scale_denominator_value;
  uint32_t maximum_physical_width;
  uint32_t maximum_physical_height;
  uint64_t reserved[4];
} gwipc_output_descriptor_upsert;

typedef struct gwipc_output_mode_upsert {
  size_t struct_size;
  uint64_t output_id;
  uint64_t mode_id;
  uint32_t physical_width;
  uint32_t physical_height;
  uint32_t refresh_millihertz;
  uint8_t preferred;
  uint8_t current;
  uint16_t reserved16;
  uint32_t flags;
  uint64_t reserved[4];
} gwipc_output_mode_upsert;

typedef enum gwipc_surface_scale_mode {
  GWIPC_SURFACE_SCALE_LEGACY = 1,
  GWIPC_SURFACE_SCALE_SCALED_PIXMAP = 2,
} gwipc_surface_scale_mode;

typedef struct gwipc_surface_output_state {
  size_t struct_size;
  uint64_t surface_id;
  uint64_t primary_output_id;
  const uint64_t *output_ids;
  size_t output_count;
  uint32_t preferred_scale_numerator;
  uint32_t preferred_scale_denominator;
  uint32_t client_buffer_scale;
  gwipc_surface_scale_mode scale_mode;
  uint64_t layout_generation;
  uint32_t flags;
  uint64_t reserved[4];
} gwipc_surface_output_state;

typedef struct gwipc_policy_output_upsert {
  size_t struct_size;
  uint64_t output_id;
  int32_t logical_x;
  int32_t logical_y;
  uint32_t logical_width;
  uint32_t logical_height;
  int32_t work_x;
  int32_t work_y;
  uint32_t work_width;
  uint32_t work_height;
  uint32_t scale_numerator;
  uint32_t scale_denominator;
  gwipc_transform transform;
  uint8_t enabled;
  uint8_t primary;
  uint16_t reserved16;
  uint32_t flags;
  uint64_t reserved[4];
} gwipc_policy_output_upsert;

typedef struct gwipc_policy_window_output_hint {
  size_t struct_size;
  uint32_t window_id;
  uint64_t previous_output_id;
  uint64_t preferred_output_id;
  uint32_t flags;
  uint64_t reserved[4];
} gwipc_policy_window_output_hint;

enum {
  GWIPC_OUTPUT_QUERY_DESCRIPTORS = UINT32_C(1) << 0,
  GWIPC_OUTPUT_QUERY_MODES = UINT32_C(1) << 1,
  GWIPC_OUTPUT_QUERY_LAYOUT = UINT32_C(1) << 2,
  GWIPC_OUTPUT_QUERY_WINDOWS = UINT32_C(1) << 3,
  GWIPC_OUTPUT_QUERY_VRR = UINT32_C(1) << 4,
};

typedef struct gwipc_output_state_query {
  size_t struct_size;
  uint64_t query_id;
  uint32_t flags;
  uint64_t reserved[4];
} gwipc_output_state_query;

typedef struct gwipc_output_configuration_commit {
  size_t struct_size;
  uint64_t configuration_id;
  uint64_t base_generation;
  uint64_t primary_output_id;
  uint32_t flags;
  uint64_t reserved[4];
} gwipc_output_configuration_commit;

typedef enum gwipc_output_configuration_result {
  GWIPC_OUTPUT_CONFIGURATION_ACCEPTED = 1,
  GWIPC_OUTPUT_CONFIGURATION_STALE_GENERATION = 2,
  GWIPC_OUTPUT_CONFIGURATION_BUSY = 3,
  GWIPC_OUTPUT_CONFIGURATION_INVALID_LAYOUT = 4,
  GWIPC_OUTPUT_CONFIGURATION_UNKNOWN_OUTPUT = 5,
  GWIPC_OUTPUT_CONFIGURATION_UNSUPPORTED_MODE = 6,
  GWIPC_OUTPUT_CONFIGURATION_UNSUPPORTED_SCALE = 7,
  GWIPC_OUTPUT_CONFIGURATION_UNSUPPORTED_TRANSFORM = 8,
  GWIPC_OUTPUT_CONFIGURATION_POLICY_REJECTED = 9,
  GWIPC_OUTPUT_CONFIGURATION_COMPOSITOR_REJECTED = 10,
  GWIPC_OUTPUT_CONFIGURATION_PRESENTER_REJECTED = 11,
  GWIPC_OUTPUT_CONFIGURATION_INTERNAL_ERROR = 12,
  GWIPC_OUTPUT_CONFIGURATION_UNSUPPORTED_VRR = 13,
  GWIPC_OUTPUT_CONFIGURATION_VRR_POLICY_REJECTED = 14,
  GWIPC_OUTPUT_CONFIGURATION_VRR_PRESENTER_REJECTED = 15,
} gwipc_output_configuration_result;

typedef struct gwipc_output_configuration_acknowledged {
  size_t struct_size;
  uint64_t request_id;
  uint64_t applied_generation;
  gwipc_output_configuration_result result;
  uint32_t flags;
  uint64_t primary_output_id;
  uint32_t root_logical_width;
  uint32_t root_logical_height;
  uint32_t enabled_output_count;
  uint32_t reserved32;
  uint64_t reserved[4];
} gwipc_output_configuration_acknowledged;

#define GWIPC_DECLARE_OUTPUT_CONTRACT(Type, name)                              \
  GWIPC_API gwipc_status gwipc_contract_encode_##name(                         \
      const Type *, gwipc_contract_payload **);                                \
  GWIPC_API const Type *gwipc_decoded_##name(const gwipc_decoded_contract *)

GWIPC_DECLARE_OUTPUT_CONTRACT(gwipc_output_descriptor_upsert,
                              output_descriptor_upsert);
GWIPC_DECLARE_OUTPUT_CONTRACT(gwipc_output_mode_upsert, output_mode_upsert);
GWIPC_DECLARE_OUTPUT_CONTRACT(gwipc_surface_output_state, surface_output_state);
GWIPC_DECLARE_OUTPUT_CONTRACT(gwipc_policy_output_upsert, policy_output_upsert);
GWIPC_DECLARE_OUTPUT_CONTRACT(gwipc_policy_window_output_hint,
                              policy_window_output_hint);
GWIPC_DECLARE_OUTPUT_CONTRACT(gwipc_output_state_query, output_state_query);
GWIPC_DECLARE_OUTPUT_CONTRACT(gwipc_output_configuration_commit,
                              output_configuration_commit);
GWIPC_DECLARE_OUTPUT_CONTRACT(gwipc_output_configuration_acknowledged,
                              output_configuration_acknowledged);

#undef GWIPC_DECLARE_OUTPUT_CONTRACT

#ifdef __cplusplus
}
#endif

#endif
