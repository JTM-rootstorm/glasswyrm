#define GWIPC_BUILDING_LIBRARY
#include <glasswyrm/ipc.h>

extern gwipc_api_version gwipc_rust_get_api_version(void);
extern gwipc_wire_version gwipc_rust_get_max_wire_version(void);
extern const char *gwipc_rust_status_string(int status);
extern int gwipc_rust_contract_encode_surface_remove(
    const gwipc_surface_remove *value, gwipc_contract_payload **out_payload);
extern const uint8_t *gwipc_rust_contract_payload_data(
    const gwipc_contract_payload *payload, size_t *out_size);
extern void gwipc_rust_contract_payload_destroy(gwipc_contract_payload *payload);
extern int gwipc_rust_contract_decode_parts(
    uint16_t message_type, const uint8_t *payload, size_t payload_size,
    gwipc_decoded_contract **out_contract);
extern uint16_t gwipc_rust_decoded_contract_type(
    const gwipc_decoded_contract *contract);
extern const gwipc_surface_remove *gwipc_rust_decoded_surface_remove(
    const gwipc_decoded_contract *contract);
extern void gwipc_rust_decoded_contract_destroy(
    gwipc_decoded_contract *contract);

gwipc_api_version gwipc_get_api_version(void) {
  return gwipc_rust_get_api_version();
}

gwipc_wire_version gwipc_get_max_wire_version(void) {
  return gwipc_rust_get_max_wire_version();
}

const char *gwipc_status_string(gwipc_status status) {
  return gwipc_rust_status_string((int)status);
}

gwipc_status gwipc_contract_encode_surface_remove(
    const gwipc_surface_remove *value, gwipc_contract_payload **out_payload) {
  return (gwipc_status)gwipc_rust_contract_encode_surface_remove(value,
                                                                  out_payload);
}

const uint8_t *gwipc_contract_payload_data(
    const gwipc_contract_payload *payload, size_t *out_size) {
  return gwipc_rust_contract_payload_data(payload, out_size);
}

void gwipc_contract_payload_destroy(gwipc_contract_payload *payload) {
  gwipc_rust_contract_payload_destroy(payload);
}

gwipc_status gwipc_contract_decode_message(
    const gwipc_message *message, gwipc_decoded_contract **out_contract) {
  if (message == NULL || out_contract == NULL)
    return GWIPC_STATUS_INVALID_ARGUMENT;
  *out_contract = NULL;
  size_t payload_size = 0;
  const uint8_t *payload = gwipc_message_payload(message, &payload_size);
  return (gwipc_status)gwipc_rust_contract_decode_parts(
      gwipc_message_type(message), payload, payload_size, out_contract);
}

uint16_t gwipc_decoded_contract_type(
    const gwipc_decoded_contract *contract) {
  return gwipc_rust_decoded_contract_type(contract);
}

const gwipc_surface_remove *gwipc_decoded_surface_remove(
    const gwipc_decoded_contract *contract) {
  return gwipc_rust_decoded_surface_remove(contract);
}

void gwipc_decoded_contract_destroy(gwipc_decoded_contract *contract) {
  gwipc_rust_decoded_contract_destroy(contract);
}
