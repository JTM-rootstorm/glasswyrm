#include <glasswyrm/ipc.h>

int main(void) {
  const gwipc_api_version api = gwipc_get_api_version();
  if (api.major != 0 || api.minor < 6 ||
      GWIPC_CAP_SESSION_STATE != (UINT64_C(1) << 13) ||
      GWIPC_CAP_INTERACTIVE_POLICY != (UINT64_C(1) << 14) ||
      GWIPC_CAP_CURSOR_SURFACE != (UINT64_C(1) << 15) ||
      GWIPC_MESSAGE_SESSION_STATE_CHANGE != 0x0400 ||
      GWIPC_SURFACE_PRESENTATION_CURSOR != (UINT32_C(1) << 1))
    return 1;

  gwipc_session_state_change change = {0};
  change.struct_size = sizeof(change);
  change.generation = 1;
  change.state = GWIPC_SESSION_INACTIVE;
  gwipc_contract_payload *payload = NULL;
  if (gwipc_contract_encode_session_state_change(&change, &payload) !=
      GWIPC_STATUS_OK)
    return 1;
  gwipc_contract_payload_destroy(payload);
  return 0;
}
