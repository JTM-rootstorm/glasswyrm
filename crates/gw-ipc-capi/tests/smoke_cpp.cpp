#include <glasswyrm/ipc.h>

#include <array>
#include <cstdint>
#include <cstring>

struct gwipc_message {
  std::uint16_t type;
  const std::uint8_t *payload;
  std::size_t payload_size;
};

int main() {
  const auto api = gwipc_get_api_version();
  const auto wire = gwipc_get_max_wire_version();
  if (api.major != 0 || api.minor != 9 || api.patch != 0 || wire.major != 1 ||
      wire.minor != 0 || std::strcmp(gwipc_status_string(GWIPC_STATUS_PROTOCOL_ERROR),
                                    "ProtocolError") != 0)
    return 1;

  gwipc_surface_remove value{};
  value.struct_size = sizeof(value);
  value.surface_id = UINT64_C(0x8877665544332211);
  gwipc_contract_payload *payload = nullptr;
  if (gwipc_contract_encode_surface_remove(&value, &payload) != GWIPC_STATUS_OK)
    return 2;

  std::size_t size = 0;
  const auto *bytes = gwipc_contract_payload_data(payload, &size);
  const std::array<std::uint8_t, 8> expected{0x11, 0x22, 0x33, 0x44,
                                             0x55, 0x66, 0x77, 0x88};
  if (size != expected.size() || std::memcmp(bytes, expected.data(), size) != 0)
    return 3;

  const gwipc_message message{GWIPC_MESSAGE_SURFACE_REMOVE, bytes, size};
  gwipc_decoded_contract *decoded = nullptr;
  if (gwipc_contract_decode_message(&message, &decoded) != GWIPC_STATUS_OK)
    return 4;
  const auto *round_trip = gwipc_decoded_surface_remove(decoded);
  if (round_trip == nullptr || round_trip->surface_id != value.surface_id)
    return 5;

  gwipc_decoded_contract_destroy(decoded);
  gwipc_contract_payload_destroy(payload);
  return 0;
}
