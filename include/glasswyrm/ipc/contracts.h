#ifndef GLASSWYRM_IPC_CONTRACTS_H
#define GLASSWYRM_IPC_CONTRACTS_H

#include <glasswyrm/ipc/message.h>

#define GWIPC_DEFAULT_MAXIMUM_PAYLOAD UINT32_C(65536)
#define GWIPC_DEFAULT_MAXIMUM_FDS UINT16_C(4)
#define GWIPC_DEFAULT_MAXIMUM_QUEUED_BYTES UINT32_C(1048576)
#define GWIPC_DEFAULT_MAXIMUM_QUEUED_MESSAGES UINT16_C(256)
#define GWIPC_HARD_MAXIMUM_PAYLOAD UINT32_C(1048576)
#define GWIPC_HARD_MAXIMUM_FDS UINT16_C(16)
#define GWIPC_HARD_MAXIMUM_QUEUED_BYTES UINT32_C(16777216)
#define GWIPC_MAXIMUM_DAMAGE_RECTANGLES ((size_t)1024)
#define GWIPC_OPACITY_ONE UINT32_C(0x00010000)
#define GWIPC_SURFACE_PRESENTATION_METADATA_ONLY UINT32_C(1)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gwipc_contract_payload gwipc_contract_payload;
typedef struct gwipc_decoded_contract gwipc_decoded_contract;

typedef enum gwipc_transform { GWIPC_TRANSFORM_NORMAL, GWIPC_TRANSFORM_ROTATE_90,
  GWIPC_TRANSFORM_ROTATE_180, GWIPC_TRANSFORM_ROTATE_270, GWIPC_TRANSFORM_FLIPPED,
  GWIPC_TRANSFORM_FLIPPED_90, GWIPC_TRANSFORM_FLIPPED_180, GWIPC_TRANSFORM_FLIPPED_270 } gwipc_transform;
typedef enum gwipc_sdr_color_space { GWIPC_SDR_COLOR_SPACE_SRGB = 1, GWIPC_SDR_COLOR_SPACE_DISPLAY_P3 } gwipc_sdr_color_space;
typedef enum gwipc_transfer_function { GWIPC_TRANSFER_FUNCTION_SRGB = 1, GWIPC_TRANSFER_FUNCTION_LINEAR } gwipc_transfer_function;
typedef enum gwipc_color_primaries { GWIPC_COLOR_PRIMARIES_SRGB = 1, GWIPC_COLOR_PRIMARIES_DISPLAY_P3 } gwipc_color_primaries;
typedef enum gwipc_tri_state { GWIPC_TRI_STATE_UNKNOWN, GWIPC_TRI_STATE_FALSE, GWIPC_TRI_STATE_TRUE } gwipc_tri_state;
typedef enum gwipc_pixel_format { GWIPC_PIXEL_FORMAT_XRGB8888 = 1, GWIPC_PIXEL_FORMAT_ARGB8888 } gwipc_pixel_format;
typedef enum gwipc_alpha_semantics { GWIPC_ALPHA_OPAQUE = 1, GWIPC_ALPHA_PREMULTIPLIED } gwipc_alpha_semantics;
typedef enum gwipc_synchronization_mode { GWIPC_SYNCHRONIZATION_NONE = 0 } gwipc_synchronization_mode;
typedef enum gwipc_buffer_release_reason { GWIPC_BUFFER_RELEASE_REPLACED = 1, GWIPC_BUFFER_RELEASE_SURFACE_REMOVED, GWIPC_BUFFER_RELEASE_CONSUMER_DONE, GWIPC_BUFFER_RELEASE_INVALID } gwipc_buffer_release_reason;
typedef enum gwipc_frame_result { GWIPC_FRAME_ACCEPTED = 1, GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA, GWIPC_FRAME_REJECTED_INVALID_BUFFER, GWIPC_FRAME_REJECTED_UNKNOWN_SURFACE, GWIPC_FRAME_DROPPED } gwipc_frame_result;

typedef struct gwipc_sdr_color_metadata { gwipc_sdr_color_space color_space; gwipc_transfer_function transfer_function; gwipc_color_primaries primaries; uint8_t luminance_available; uint32_t minimum_luminance_millinit; uint32_t maximum_luminance_millinit; uint32_t max_frame_average_luminance_millinit; } gwipc_sdr_color_metadata;
typedef struct gwipc_output_upsert { size_t struct_size; uint64_t output_id; uint8_t enabled; int32_t logical_x, logical_y; uint32_t logical_width, logical_height, physical_pixel_width, physical_pixel_height, refresh_millihertz, scale_numerator, scale_denominator; gwipc_transform transform; gwipc_sdr_color_metadata color; uint64_t reserved[4]; } gwipc_output_upsert;
typedef struct gwipc_output_remove { size_t struct_size; uint64_t output_id; uint64_t reserved[4]; } gwipc_output_remove;
typedef struct gwipc_surface_upsert { size_t struct_size; uint64_t surface_id; uint32_t x11_window_id; uint64_t parent_surface_id, output_id; int32_t logical_x, logical_y; uint32_t logical_width, logical_height; int32_t stacking; uint8_t visible, clipping; int32_t clip_x, clip_y; uint32_t clip_width, clip_height; gwipc_transform transform; uint32_t opacity, scale_numerator, scale_denominator; gwipc_sdr_color_metadata color; uint32_t presentation_flags; gwipc_tri_state fullscreen_eligible, direct_scanout_eligible; uint64_t reserved[4]; } gwipc_surface_upsert;
typedef struct gwipc_surface_remove { size_t struct_size; uint64_t surface_id; uint64_t reserved[4]; } gwipc_surface_remove;
typedef struct gwipc_buffer_attach { size_t struct_size; uint64_t buffer_id, surface_id; uint32_t width, height, stride; uint64_t byte_offset, storage_size; gwipc_pixel_format pixel_format; uint64_t modifier; gwipc_alpha_semantics alpha_semantics; gwipc_sdr_color_metadata color; gwipc_synchronization_mode synchronization; uint32_t flags; uint64_t reserved[4]; } gwipc_buffer_attach;
typedef struct gwipc_buffer_detach { size_t struct_size; uint64_t surface_id, buffer_id; uint64_t reserved[4]; } gwipc_buffer_detach;
typedef struct gwipc_buffer_release { size_t struct_size; uint64_t buffer_id; gwipc_buffer_release_reason reason; uint64_t reserved[4]; } gwipc_buffer_release;
typedef struct gwipc_damage_rectangle { int32_t x, y; uint32_t width, height; } gwipc_damage_rectangle;
typedef struct gwipc_surface_damage { size_t struct_size; uint64_t surface_id; const gwipc_damage_rectangle *rectangles; size_t rectangle_count; uint64_t reserved[4]; } gwipc_surface_damage;
typedef struct gwipc_frame_commit { size_t struct_size; uint64_t commit_id, output_id, producer_generation; uint32_t flags; uint64_t reserved[4]; } gwipc_frame_commit;
typedef struct gwipc_frame_acknowledged { size_t struct_size; uint64_t commit_id, output_id, presented_generation; gwipc_frame_result result; uint64_t reserved[4]; } gwipc_frame_acknowledged;

#define GWIPC_DECLARE_CONTRACT(Type, name) \
  GWIPC_API gwipc_status gwipc_contract_encode_##name(const Type *value, gwipc_contract_payload **out_payload); \
  GWIPC_API const Type *gwipc_decoded_##name(const gwipc_decoded_contract *contract)
GWIPC_DECLARE_CONTRACT(gwipc_output_upsert, output_upsert);
GWIPC_DECLARE_CONTRACT(gwipc_output_remove, output_remove);
GWIPC_DECLARE_CONTRACT(gwipc_surface_upsert, surface_upsert);
GWIPC_DECLARE_CONTRACT(gwipc_surface_remove, surface_remove);
GWIPC_DECLARE_CONTRACT(gwipc_buffer_attach, buffer_attach);
GWIPC_DECLARE_CONTRACT(gwipc_buffer_detach, buffer_detach);
GWIPC_DECLARE_CONTRACT(gwipc_buffer_release, buffer_release);
GWIPC_DECLARE_CONTRACT(gwipc_surface_damage, surface_damage);
GWIPC_DECLARE_CONTRACT(gwipc_frame_commit, frame_commit);
GWIPC_DECLARE_CONTRACT(gwipc_frame_acknowledged, frame_acknowledged);
#undef GWIPC_DECLARE_CONTRACT

GWIPC_API const uint8_t *gwipc_contract_payload_data(const gwipc_contract_payload *payload, size_t *out_size);
GWIPC_API void gwipc_contract_payload_destroy(gwipc_contract_payload *payload);
GWIPC_API gwipc_status gwipc_contract_decode_message(const gwipc_message *message, gwipc_decoded_contract **out_contract);
GWIPC_API uint16_t gwipc_decoded_contract_type(const gwipc_decoded_contract *contract);
GWIPC_API void gwipc_decoded_contract_destroy(gwipc_decoded_contract *contract);

#ifdef __cplusplus
}
#endif
#endif
