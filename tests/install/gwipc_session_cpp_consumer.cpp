#include <glasswyrm/ipc.h>

int main() {
  const gwipc_api_version api = gwipc_get_api_version();
  if (api.major != 0 || api.minor < 6 ||
      GWIPC_MESSAGE_POLICY_BINDINGS_UPSERT != 0x0213 ||
      GWIPC_MESSAGE_SESSION_STATE_ACKNOWLEDGED != 0x0401)
    return 1;

  gwipc_policy_bindings_upsert bindings{};
  bindings.struct_size = sizeof(bindings);
  bindings.move_modifiers = 8;
  bindings.resize_modifiers = 8;
  bindings.close_modifiers = 8;
  bindings.move_button = 1;
  bindings.resize_button = 3;
  bindings.close_keysym = 0xffc1;
  bindings.minimum_width = 96;
  bindings.minimum_height = 64;
  bindings.raise_on_focus = 1;
  bindings.consume_wm_bindings = 1;
  gwipc_contract_payload *payload = nullptr;
  if (gwipc_contract_encode_policy_bindings_upsert(&bindings, &payload) !=
      GWIPC_STATUS_OK)
    return 1;
  gwipc_contract_payload_destroy(payload);
  return 0;
}
