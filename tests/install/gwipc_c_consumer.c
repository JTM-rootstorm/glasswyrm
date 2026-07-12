#include <glasswyrm/ipc.h>

#include <stddef.h>

int main(void) {
  const gwipc_api_version api = gwipc_get_api_version();
  const gwipc_wire_version wire = gwipc_get_max_wire_version();
  const gwipc_listener_options listener = {
      .struct_size = sizeof(gwipc_listener_options),
  };
  const gwipc_connection_options connection = {
      .struct_size = sizeof(gwipc_connection_options),
  };

  gwipc_output_remove remove = {.struct_size = sizeof(remove), .output_id = 7};
  gwipc_contract_payload *payload = NULL;
  if (gwipc_contract_encode_output_remove(&remove, &payload) != GWIPC_STATUS_OK)
    return 1;
  size_t payload_size = 0;
  if (gwipc_contract_payload_data(payload, &payload_size) == NULL || payload_size != 8)
    return 1;
  gwipc_contract_payload_destroy(payload);

  if (api.major != 0 || api.minor < 2 || wire.major != 1 ||
      wire.minor != 0 || listener.struct_size == 0 ||
      connection.struct_size == 0) {
    return 1;
  }
  return gwipc_status_string(GWIPC_STATUS_OK) == NULL;
}
