#include <glasswyrm/ipc.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct gwipc_message {
  uint16_t type;
  const uint8_t *payload;
  size_t payload_size;
};

int main(void) {
  const gwipc_api_version api = gwipc_get_api_version();
  const gwipc_wire_version wire = gwipc_get_max_wire_version();
  if (api.major != 0 || api.minor != 9 || api.patch != 0 || wire.major != 1 ||
      wire.minor != 0 || strcmp(gwipc_status_string(GWIPC_STATUS_OK), "Ok") != 0 ||
      strcmp(gwipc_status_string((gwipc_status)99), "UnknownStatus") != 0)
    return 1;

  gwipc_surface_remove value = {0};
  value.struct_size = sizeof(value);
  value.surface_id = UINT64_C(0x0102030405060708);
  gwipc_contract_payload *payload = NULL;
  if (gwipc_contract_encode_surface_remove(&value, &payload) != GWIPC_STATUS_OK)
    return 2;

  size_t size = 0;
  const uint8_t *bytes = gwipc_contract_payload_data(payload, &size);
  const uint8_t expected[] = {0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
  if (size != sizeof(expected) || memcmp(bytes, expected, sizeof(expected)) != 0)
    return 3;

  const gwipc_message message = {GWIPC_MESSAGE_SURFACE_REMOVE, bytes, size};
  gwipc_decoded_contract *decoded = NULL;
  if (gwipc_contract_decode_message(&message, &decoded) != GWIPC_STATUS_OK ||
      gwipc_decoded_contract_type(decoded) != GWIPC_MESSAGE_SURFACE_REMOVE)
    return 4;
  const gwipc_surface_remove *round_trip = gwipc_decoded_surface_remove(decoded);
  if (round_trip == NULL || round_trip->struct_size != sizeof(*round_trip) ||
      round_trip->surface_id != value.surface_id)
    return 5;

  gwipc_decoded_contract_destroy(decoded);
  gwipc_contract_payload_destroy(payload);
  return 0;
}
