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

  gwipc_policy_lifecycle_window_upsert lifecycle{};
  lifecycle.struct_size = sizeof(lifecycle);
  lifecycle.window.struct_size = sizeof(lifecycle.window);
  lifecycle.window.window_id = 10;
  lifecycle.window.parent_window_id = 1;
  lifecycle.window.workspace_id = 1;
  lifecycle.window.requested_width = 100;
  lifecycle.window.requested_height = 80;
  lifecycle.window.window_type = GWIPC_POLICY_WINDOW_NORMAL;
  lifecycle.window.map_intent = GWIPC_POLICY_WANTS_MAP;
  lifecycle.window.creation_serial = 1;
  lifecycle.window.map_serial = 1;
  lifecycle.stack_serial = 2;
  lifecycle.stack_mode = GWIPC_POLICY_STACK_ABOVE;
  payload = nullptr;
  ok = ok && gwipc_contract_encode_policy_lifecycle_window_upsert(
                 &lifecycle, &payload) == GWIPC_STATUS_OK;
  gwipc_contract_payload_destroy(payload);
  lifecycle.window.reserved[0] = 1;
  ok = ok && gwipc_contract_encode_policy_lifecycle_window_upsert(
                 &lifecycle, &payload) == GWIPC_STATUS_INVALID_ARGUMENT;

  gwipc_surface_policy_upsert surface{};
  surface.struct_size = sizeof(surface);
  surface.surface_id = 11;
  surface.x11_window_id = 10;
  surface.workspace_id = 1;
  surface.window_type = GWIPC_POLICY_WINDOW_NORMAL;
  surface.applied_state = GWIPC_POLICY_APPLIED_NORMAL;
  payload = nullptr;
  ok = ok && gwipc_contract_encode_surface_policy_upsert(&surface, &payload) ==
                 GWIPC_STATUS_OK;
  gwipc_contract_payload_destroy(payload);
  surface.focused = 2;
  ok = ok && gwipc_contract_encode_surface_policy_upsert(&surface, &payload) ==
                 GWIPC_STATUS_INVALID_ARGUMENT;

  const auto api = gwipc_get_api_version();
  ok = ok && api.major == 0 && api.minor == 5 && api.patch == 0;

  remove.reserved[0] = 1;
  payload = reinterpret_cast<gwipc_contract_payload*>(1);
  ok = ok && gwipc_contract_encode_output_remove(&remove, &payload) ==
                 GWIPC_STATUS_INVALID_ARGUMENT;
  if (!ok) std::fputs("public contract API test failed\n", stderr);
  return ok ? 0 : 1;
}
