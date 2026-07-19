#include <glasswyrm/ipc.h>

int main(void) {
  const gwipc_api_version api = gwipc_get_api_version();
  if (api.major != 0 || api.minor < 9 || api.patch != 0 ||
      GWIPC_CAP_VRR_METADATA != (UINT64_C(1) << 22) ||
      GWIPC_CAP_VRR_POLICY != (UINT64_C(1) << 23) ||
      GWIPC_CAP_PRESENTATION_TIMING != (UINT64_C(1) << 24) ||
      GWIPC_MESSAGE_OUTPUT_VRR_CAPABILITY_UPSERT != 0x0104 ||
      GWIPC_MESSAGE_OUTPUT_VRR_POLICY_UPSERT != 0x0105 ||
      GWIPC_MESSAGE_OUTPUT_VRR_STATE_UPSERT != 0x0106 ||
      GWIPC_MESSAGE_SURFACE_VRR_STATE != 0x0114 ||
      GWIPC_MESSAGE_PRESENTATION_TIMING != 0x0142 ||
      GWIPC_MESSAGE_POLICY_WINDOW_VRR_UPSERT != 0x0206 ||
      GWIPC_MESSAGE_POLICY_OUTPUT_VRR_UPSERT != 0x0207 ||
      GWIPC_MESSAGE_POLICY_WINDOW_VRR_STATE != 0x0214 ||
      GWIPC_MESSAGE_POLICY_OUTPUT_VRR_STATE != 0x0215 ||
      GWIPC_OUTPUT_QUERY_VRR != (UINT32_C(1) << 4) ||
      GWIPC_OUTPUT_CONFIGURATION_UNSUPPORTED_VRR != 13 ||
      GWIPC_OUTPUT_CONFIGURATION_VRR_POLICY_REJECTED != 14 ||
      GWIPC_OUTPUT_CONFIGURATION_VRR_PRESENTER_REJECTED != 15)
    return 1;

  gwipc_output_vrr_policy_upsert policy = {0};
  policy.struct_size = sizeof(policy);
  policy.output_id = 1;
  policy.mode = GWIPC_VRR_POLICY_FULLSCREEN;

  gwipc_contract_payload *payload = NULL;
  if (gwipc_contract_encode_output_vrr_policy_upsert(&policy, &payload) !=
      GWIPC_STATUS_OK)
    return 1;
  gwipc_contract_payload_destroy(payload);
  return 0;
}
