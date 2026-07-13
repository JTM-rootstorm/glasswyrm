#include <glasswyrm/ipc.h>

int main(void) {
  const gwipc_api_version api = gwipc_get_api_version();
  if (api.major != 0 || api.minor < 5 ||
      GWIPC_CAP_SYNTHETIC_INPUT != (UINT64_C(1) << 12) ||
      GWIPC_MESSAGE_SYNTHETIC_MOTION != 0x0300 ||
      GWIPC_MESSAGE_SYNTHETIC_INPUT_ACKNOWLEDGED != 0x0310)
    return 1;

  gwipc_synthetic_motion motion = {0};
  motion.struct_size = sizeof(motion);
  motion.input_id = 1;
  motion.time_ms = 2;
  motion.root_x = 10;
  motion.root_y = 20;
  gwipc_contract_payload *payload = NULL;
  if (gwipc_contract_encode_synthetic_motion(&motion, &payload) !=
      GWIPC_STATUS_OK)
    return 1;
  gwipc_contract_payload_destroy(payload);
  return 0;
}

