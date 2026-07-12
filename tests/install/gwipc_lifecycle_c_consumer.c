#include <glasswyrm/ipc.h>

int main(void) {
  const gwipc_api_version api = gwipc_get_api_version();
  if (api.major != 0 || api.minor < 4 ||
      GWIPC_CAP_WINDOW_LIFECYCLE != (UINT64_C(1) << 11) ||
      GWIPC_MESSAGE_SURFACE_POLICY_UPSERT != 0x0112 ||
      GWIPC_MESSAGE_POLICY_LIFECYCLE_WINDOW_UPSERT != 0x0203)
    return 1;
  gwipc_policy_lifecycle_window_upsert value = {0};
  value.struct_size = sizeof(value);
  value.window.struct_size = sizeof(value.window);
  value.window.window_id = 10;
  value.window.parent_window_id = 1;
  value.window.workspace_id = 1;
  value.window.requested_width = 100;
  value.window.requested_height = 80;
  value.window.window_type = GWIPC_POLICY_WINDOW_NORMAL;
  value.window.map_intent = GWIPC_POLICY_WANTS_MAP;
  value.window.creation_serial = 1;
  value.window.map_serial = 1;
  gwipc_contract_payload *payload = NULL;
  if (gwipc_contract_encode_policy_lifecycle_window_upsert(&value, &payload) !=
      GWIPC_STATUS_OK)
    return 1;
  gwipc_contract_payload_destroy(payload);
  return 0;
}
