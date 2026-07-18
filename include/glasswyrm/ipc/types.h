#ifndef GLASSWYRM_IPC_TYPES_H
#define GLASSWYRM_IPC_TYPES_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define GWIPC_API
#elif defined(GWIPC_BUILDING_LIBRARY)
#define GWIPC_API __attribute__((visibility("default")))
#else
#define GWIPC_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gwipc_listener gwipc_listener;
typedef struct gwipc_connection gwipc_connection;
typedef struct gwipc_message gwipc_message;

typedef struct gwipc_api_version {
  uint16_t major;
  uint16_t minor;
  uint16_t patch;
} gwipc_api_version;

typedef struct gwipc_wire_version {
  uint16_t major;
  uint16_t minor;
} gwipc_wire_version;

typedef enum gwipc_status {
  GWIPC_STATUS_OK = 0,
  GWIPC_STATUS_WOULD_BLOCK,
  GWIPC_STATUS_IN_PROGRESS,
  GWIPC_STATUS_DISCONNECTED,
  GWIPC_STATUS_INVALID_ARGUMENT,
  GWIPC_STATUS_INVALID_STATE,
  GWIPC_STATUS_OUT_OF_MEMORY,
  GWIPC_STATUS_LIMIT_EXCEEDED,
  GWIPC_STATUS_PROTOCOL_ERROR,
  GWIPC_STATUS_CREDENTIAL_REJECTED,
  GWIPC_STATUS_VERSION_MISMATCH,
  GWIPC_STATUS_ROLE_REJECTED,
  GWIPC_STATUS_CAPABILITY_MISMATCH,
  GWIPC_STATUS_SYSTEM_ERROR,
} gwipc_status;

typedef enum gwipc_connection_state {
  GWIPC_CONNECTION_CONNECTING = 0,
  GWIPC_CONNECTION_AWAITING_HELLO,
  GWIPC_CONNECTION_AWAITING_WELCOME,
  GWIPC_CONNECTION_ESTABLISHED,
  GWIPC_CONNECTION_REJECTING,
  GWIPC_CONNECTION_CLOSING,
  GWIPC_CONNECTION_CLOSED,
} gwipc_connection_state;

typedef enum gwipc_role {
  GWIPC_ROLE_UNKNOWN = 0,
  GWIPC_ROLE_PROTOCOL_SERVER = 1,
  GWIPC_ROLE_WINDOW_MANAGER = 2,
  GWIPC_ROLE_COMPOSITOR = 3,
  GWIPC_ROLE_TEST_PRODUCER = 4,
  GWIPC_ROLE_TEST_CONSUMER = 5,
  GWIPC_ROLE_DIAGNOSTIC_TOOL = 6,
} gwipc_role;

#define GWIPC_ROLE_BIT(role) (UINT64_C(1) << (role))

typedef uint64_t gwipc_capabilities;
enum {
  GWIPC_CAP_FD_PASSING = UINT64_C(1) << 0,
  GWIPC_CAP_SNAPSHOTS = UINT64_C(1) << 1,
  GWIPC_CAP_OUTPUT_STATE = UINT64_C(1) << 2,
  GWIPC_CAP_SURFACE_STATE = UINT64_C(1) << 3,
  GWIPC_CAP_MEMFD_BUFFERS = UINT64_C(1) << 4,
  GWIPC_CAP_DAMAGE_REGIONS = UINT64_C(1) << 5,
  GWIPC_CAP_SCALE_METADATA = UINT64_C(1) << 6,
  GWIPC_CAP_SDR_COLOR_METADATA = UINT64_C(1) << 7,
  GWIPC_CAP_FRAME_ACKNOWLEDGEMENT = UINT64_C(1) << 8,
  GWIPC_CAP_TRACE_METADATA = UINT64_C(1) << 9,
  GWIPC_CAP_WINDOW_POLICY = UINT64_C(1) << 10,
  GWIPC_CAP_WINDOW_LIFECYCLE = UINT64_C(1) << 11,
  GWIPC_CAP_SYNTHETIC_INPUT = UINT64_C(1) << 12,
  GWIPC_CAP_SESSION_STATE = UINT64_C(1) << 13,
  GWIPC_CAP_INTERACTIVE_POLICY = UINT64_C(1) << 14,
  GWIPC_CAP_CURSOR_SURFACE = UINT64_C(1) << 15,
  GWIPC_CAP_CPU_BUFFER_SYNCHRONIZATION = UINT64_C(1) << 16,
};

enum {
  GWIPC_MESSAGE_HELLO = 0x0001,
  GWIPC_MESSAGE_WELCOME = 0x0002,
  GWIPC_MESSAGE_REJECT = 0x0003,
  GWIPC_MESSAGE_PING = 0x0004,
  GWIPC_MESSAGE_PONG = 0x0005,
  GWIPC_MESSAGE_PROTOCOL_ERROR = 0x0006,
  GWIPC_MESSAGE_SNAPSHOT_BEGIN = 0x0010,
  GWIPC_MESSAGE_SNAPSHOT_END = 0x0011,
  GWIPC_MESSAGE_SNAPSHOT_ABORT = 0x0012,
  GWIPC_MESSAGE_OUTPUT_UPSERT = 0x0100,
  GWIPC_MESSAGE_OUTPUT_REMOVE = 0x0101,
  GWIPC_MESSAGE_SURFACE_UPSERT = 0x0110,
  GWIPC_MESSAGE_SURFACE_REMOVE = 0x0111,
  GWIPC_MESSAGE_SURFACE_POLICY_UPSERT = 0x0112,
  GWIPC_MESSAGE_BUFFER_ATTACH = 0x0120,
  GWIPC_MESSAGE_BUFFER_DETACH = 0x0121,
  GWIPC_MESSAGE_BUFFER_RELEASE = 0x0122,
  GWIPC_MESSAGE_SURFACE_DAMAGE = 0x0130,
  GWIPC_MESSAGE_FRAME_COMMIT = 0x0140,
  GWIPC_MESSAGE_FRAME_ACKNOWLEDGED = 0x0141,
  GWIPC_MESSAGE_POLICY_CONTEXT_UPSERT = 0x0200,
  GWIPC_MESSAGE_POLICY_WINDOW_UPSERT = 0x0201,
  GWIPC_MESSAGE_POLICY_WINDOW_REMOVE = 0x0202,
  GWIPC_MESSAGE_POLICY_LIFECYCLE_WINDOW_UPSERT = 0x0203,
  GWIPC_MESSAGE_POLICY_COMMIT = 0x0210,
  GWIPC_MESSAGE_POLICY_WINDOW_STATE = 0x0211,
  GWIPC_MESSAGE_POLICY_ACKNOWLEDGED = 0x0212,
  GWIPC_MESSAGE_POLICY_BINDINGS_UPSERT = 0x0213,
  GWIPC_MESSAGE_SYNTHETIC_MOTION = 0x0300,
  GWIPC_MESSAGE_SYNTHETIC_BUTTON = 0x0301,
  GWIPC_MESSAGE_SYNTHETIC_KEY = 0x0302,
  GWIPC_MESSAGE_SYNTHETIC_BARRIER = 0x0303,
  GWIPC_MESSAGE_SYNTHETIC_INPUT_ACKNOWLEDGED = 0x0310,
  GWIPC_MESSAGE_SESSION_STATE_CHANGE = 0x0400,
  GWIPC_MESSAGE_SESSION_STATE_ACKNOWLEDGED = 0x0401,
};

enum {
  GWIPC_FLAG_REPLY = UINT32_C(1) << 0,
  GWIPC_FLAG_ERROR = UINT32_C(1) << 1,
  GWIPC_FLAG_ACK_REQUIRED = UINT32_C(1) << 2,
  GWIPC_FLAG_SNAPSHOT_ITEM = UINT32_C(1) << 3,
  GWIPC_FLAG_CRITICAL = UINT32_C(1) << 4,
};

typedef struct gwipc_peer_info {
  int32_t pid;
  uint32_t uid;
  uint32_t gid;
  gwipc_role role;
  gwipc_wire_version wire_version;
  gwipc_capabilities capabilities;
  uint32_t maximum_payload;
  uint16_t maximum_fd_count;
  uint64_t connection_id;
} gwipc_peer_info;

typedef struct gwipc_listener_options {
  size_t struct_size;
  const char *path;
  gwipc_role local_role;
  uint64_t accepted_peer_roles;
  gwipc_capabilities offered_capabilities;
  gwipc_capabilities required_peer_capabilities;
  uint32_t maximum_payload;
  uint16_t maximum_fd_count;
  uint8_t require_same_uid;
  uint8_t allow_any_uid_for_tests;
  uint32_t maximum_queued_bytes;
  uint16_t maximum_queued_messages;
  const char *instance_label;
  uint64_t reserved[4];
} gwipc_listener_options;

typedef struct gwipc_connection_options {
  size_t struct_size;
  const char *path;
  gwipc_role local_role;
  uint64_t acceptable_server_roles;
  gwipc_capabilities offered_capabilities;
  gwipc_capabilities required_peer_capabilities;
  uint32_t maximum_payload;
  uint16_t maximum_fd_count;
  uint32_t maximum_queued_bytes;
  uint16_t maximum_queued_messages;
  const char *instance_label;
  uint64_t reserved[4];
} gwipc_connection_options;

typedef struct gwipc_outgoing_message {
  size_t struct_size;
  uint16_t type;
  uint32_t flags;
  uint64_t reply_to;
  const uint8_t *payload;
  size_t payload_size;
  const int *fds;
  size_t fd_count;
  uint64_t reserved[4];
} gwipc_outgoing_message;

#ifdef __cplusplus
}
#endif

#endif
