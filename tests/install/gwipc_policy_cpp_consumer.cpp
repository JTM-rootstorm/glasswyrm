#include <glasswyrm/ipc.hpp>

#include <cstddef>
#include <memory>

namespace {

struct ContractPayloadDeleter {
  void operator()(gwipc_contract_payload* value) const noexcept {
    gwipc_contract_payload_destroy(value);
  }
};
struct ControlPayloadDeleter {
  void operator()(gwipc_control_payload* value) const noexcept {
    gwipc_control_payload_destroy(value);
  }
};

bool valid(gwipc_contract_payload* raw) {
  const std::unique_ptr<gwipc_contract_payload, ContractPayloadDeleter> payload(raw);
  std::size_t size = 0;
  return gwipc_contract_payload_data(payload.get(), &size) != nullptr && size != 0;
}

bool valid(gwipc_control_payload* raw) {
  const std::unique_ptr<gwipc_control_payload, ControlPayloadDeleter> payload(raw);
  std::size_t size = 0;
  return gwipc_control_payload_data(payload.get(), &size) != nullptr && size != 0;
}

} // namespace

int main() {
  const auto api = gwipc_get_api_version();
  const auto wire = gwipc_get_max_wire_version();
  if (api.major != 0 || api.minor < 3 || wire.major != 1 || wire.minor != 0 ||
      GWIPC_CAP_WINDOW_POLICY != (UINT64_C(1) << 10))
    return 1;

  gwipc_snapshot_abort abort{sizeof(abort), 4, 1, "replacement", 11, {}};
  gwipc_control_payload* control = nullptr;
  if (gwipc_control_encode_snapshot_abort(&abort, &control) != GWIPC_STATUS_OK ||
      !valid(control))
    return 1;

  gwipc_policy_window_state state{};
  state.struct_size = sizeof(state);
  state.window_id = 1001;
  state.workspace_id = 1;
  state.output_id = 9;
  state.final_width = 320;
  state.final_height = 240;
  state.stacking = 0;
  state.window_type = GWIPC_POLICY_WINDOW_NORMAL;
  state.applied_state = GWIPC_POLICY_APPLIED_NORMAL;
  state.visible = 1;
  state.focused = 1;
  state.managed = 1;
  state.decoration_eligible = 1;
  state.fullscreen_eligible = GWIPC_TRI_STATE_FALSE;
  state.direct_scanout_eligible = GWIPC_TRI_STATE_UNKNOWN;
  gwipc_contract_payload* contract = nullptr;
  if (gwipc_contract_encode_policy_window_state(&state, &contract) !=
          GWIPC_STATUS_OK ||
      !valid(contract))
    return 1;

  gwipc_policy_acknowledged acknowledged{};
  acknowledged.struct_size = sizeof(acknowledged);
  acknowledged.commit_id = 100;
  acknowledged.producer_generation = 7;
  acknowledged.applied_generation = 7;
  acknowledged.policy_hash = UINT64_C(0x123456789abcdef0);
  acknowledged.window_count = 1;
  acknowledged.result = GWIPC_POLICY_ACCEPTED;
  contract = nullptr;
  if (gwipc_contract_encode_policy_acknowledged(&acknowledged, &contract) !=
          GWIPC_STATUS_OK ||
      !valid(contract))
    return 1;

  gwipc_policy_window_remove remove{sizeof(remove), 1001, {}};
  contract = nullptr;
  return gwipc_contract_encode_policy_window_remove(&remove, &contract) !=
                 GWIPC_STATUS_OK ||
             !valid(contract);
}
