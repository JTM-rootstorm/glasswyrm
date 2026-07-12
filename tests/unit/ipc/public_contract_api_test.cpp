#include <glasswyrm/ipc.h>

#include <array>
#include <cstdint>
#include <cstdio>

int main() {
  gwipc_output_remove remove{sizeof(remove), 0x0102030405060708ULL, {}};
  gwipc_contract_payload* payload = nullptr;
  if (gwipc_contract_encode_output_remove(&remove, &payload) != GWIPC_STATUS_OK)
    return 1;
  size_t size = 0;
  const auto* data = gwipc_contract_payload_data(payload, &size);
  constexpr std::array<std::uint8_t, 8> golden{8, 7, 6, 5, 4, 3, 2, 1};
  bool ok = size == golden.size();
  for (size_t i = 0; ok && i < size; ++i) ok = data[i] == golden[i];
  gwipc_contract_payload_destroy(payload);

  remove.reserved[0] = 1;
  payload = reinterpret_cast<gwipc_contract_payload*>(1);
  ok = ok && gwipc_contract_encode_output_remove(&remove, &payload) ==
                 GWIPC_STATUS_INVALID_ARGUMENT;
  if (!ok) std::fputs("public contract API test failed\n", stderr);
  return ok ? 0 : 1;
}
