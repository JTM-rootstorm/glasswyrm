#include <glasswyrm/ipc.h>

int main() {
  gwipc_surface_policy_upsert value{};
  value.struct_size = sizeof(value);
  value.surface_id = 11;
  value.x11_window_id = 10;
  value.workspace_id = 1;
  value.window_type = GWIPC_POLICY_WINDOW_NORMAL;
  value.applied_state = GWIPC_POLICY_APPLIED_NORMAL;
  gwipc_contract_payload* payload = nullptr;
  if (gwipc_contract_encode_surface_policy_upsert(&value, &payload) !=
      GWIPC_STATUS_OK)
    return 1;
  gwipc_contract_payload_destroy(payload);
  return 0;
}
