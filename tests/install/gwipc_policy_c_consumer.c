#include <glasswyrm/ipc.h>

#include <stddef.h>

static int valid_control_payload(gwipc_control_payload *payload) {
  size_t size = 0;
  const uint8_t *bytes = gwipc_control_payload_data(payload, &size);
  gwipc_control_payload_destroy(payload);
  return bytes != NULL && size != 0;
}

static int valid_contract_payload(gwipc_contract_payload *payload) {
  size_t size = 0;
  const uint8_t *bytes = gwipc_contract_payload_data(payload, &size);
  gwipc_contract_payload_destroy(payload);
  return bytes != NULL && size != 0;
}

int main(void) {
  const gwipc_api_version api = gwipc_get_api_version();
  const gwipc_wire_version wire = gwipc_get_max_wire_version();
  if (api.major != 0 || api.minor < 3 || wire.major != 1 || wire.minor != 0 ||
      GWIPC_CAP_WINDOW_POLICY != (UINT64_C(1) << 10) ||
      GWIPC_MESSAGE_POLICY_CONTEXT_UPSERT != 0x0200 ||
      GWIPC_MESSAGE_POLICY_ACKNOWLEDGED != 0x0212) {
    return 1;
  }

  gwipc_snapshot_begin begin = {
      .struct_size = sizeof(begin),
      .snapshot_id = 1,
      .domain = GWIPC_SNAPSHOT_WINDOW_POLICY,
      .generation = 7,
      .expected_item_count = 2,
  };
  gwipc_control_payload *control = NULL;
  if (gwipc_control_encode_snapshot_begin(&begin, &control) != GWIPC_STATUS_OK ||
      !valid_control_payload(control)) {
    return 1;
  }

  gwipc_snapshot_end end = {
      .struct_size = sizeof(end),
      .snapshot_id = 1,
      .generation = 7,
      .actual_item_count = 2,
  };
  control = NULL;
  if (gwipc_control_encode_snapshot_end(&end, &control) != GWIPC_STATUS_OK ||
      !valid_control_payload(control)) {
    return 1;
  }

  gwipc_policy_context_upsert context = {
      .struct_size = sizeof(context),
      .root_window_id = 1,
      .workspace_id = 1,
      .output_id = 9,
      .work_width = 1024,
      .work_height = 768,
  };
  gwipc_contract_payload *contract = NULL;
  if (gwipc_contract_encode_policy_context_upsert(&context, &contract) !=
          GWIPC_STATUS_OK ||
      !valid_contract_payload(contract)) {
    return 1;
  }

  gwipc_policy_window_upsert window = {
      .struct_size = sizeof(window),
      .window_id = 1001,
      .parent_window_id = 1,
      .workspace_id = 1,
      .requested_width = 320,
      .requested_height = 240,
      .window_type = GWIPC_POLICY_WINDOW_NORMAL,
      .map_intent = GWIPC_POLICY_WANTS_MAP,
      .decoration_preference = GWIPC_TRI_STATE_UNKNOWN,
      .creation_serial = 1,
      .map_serial = 1,
  };
  contract = NULL;
  if (gwipc_contract_encode_policy_window_upsert(&window, &contract) !=
          GWIPC_STATUS_OK ||
      !valid_contract_payload(contract)) {
    return 1;
  }

  gwipc_policy_commit commit = {
      .struct_size = sizeof(commit),
      .commit_id = 100,
      .producer_generation = 7,
  };
  contract = NULL;
  if (gwipc_contract_encode_policy_commit(&commit, &contract) !=
          GWIPC_STATUS_OK ||
      !valid_contract_payload(contract)) {
    return 1;
  }

  return 0;
}
