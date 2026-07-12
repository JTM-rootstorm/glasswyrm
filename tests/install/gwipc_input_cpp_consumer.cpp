#include <glasswyrm/ipc.h>

#include <cstdint>

int main() {
  const gwipc_api_version api = gwipc_get_api_version();
  if (api.major != 0 || api.minor < 5 ||
      GWIPC_CAP_SYNTHETIC_INPUT != (UINT64_C(1) << 12) ||
      GWIPC_MESSAGE_SYNTHETIC_KEY != 0x0302)
    return 1;

  gwipc_synthetic_key key{};
  key.struct_size = sizeof(key);
  key.input_id = 1;
  key.time_ms = 2;
  key.keycode = 38;
  key.pressed = 1;
  gwipc_contract_payload *payload = nullptr;
  if (gwipc_contract_encode_synthetic_key(&key, &payload) != GWIPC_STATUS_OK)
    return 1;
  gwipc_contract_payload_destroy(payload);
  return 0;
}
