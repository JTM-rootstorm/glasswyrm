#include <glasswyrm/ipc.h>

#include <string.h>

int main(void) {
  const gwipc_api_version api = gwipc_get_api_version();
  if (api.major != 0 || api.minor < 8 || api.patch != 0 ||
      GWIPC_CAP_OUTPUT_MANAGEMENT != (UINT64_C(1) << 17) ||
      GWIPC_CAP_MULTI_OUTPUT_POLICY != (UINT64_C(1) << 18) ||
      GWIPC_CAP_SURFACE_OUTPUT_MEMBERSHIP != (UINT64_C(1) << 19) ||
      GWIPC_CAP_SCALE_AWARE_SURFACES != (UINT64_C(1) << 20) ||
      GWIPC_CAP_OUTPUT_CONTROL != (UINT64_C(1) << 21) ||
      GWIPC_MESSAGE_OUTPUT_DESCRIPTOR_UPSERT != 0x0102 ||
      GWIPC_MESSAGE_OUTPUT_MODE_UPSERT != 0x0103 ||
      GWIPC_MESSAGE_SURFACE_OUTPUT_STATE != 0x0113 ||
      GWIPC_MESSAGE_POLICY_OUTPUT_UPSERT != 0x0204 ||
      GWIPC_MESSAGE_POLICY_WINDOW_OUTPUT_HINT != 0x0205 ||
      GWIPC_MESSAGE_OUTPUT_STATE_QUERY != 0x0500 ||
      GWIPC_MESSAGE_OUTPUT_CONFIGURATION_COMMIT != 0x0501 ||
      GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED != 0x0502 ||
      GWIPC_MAXIMUM_OUTPUTS != 8U || GWIPC_MAXIMUM_OUTPUT_NAME_BYTES != 63U ||
      GWIPC_MAXIMUM_OUTPUT_MODES != 128U ||
      GWIPC_MAXIMUM_OUTPUT_SCALE_DENOMINATOR != 120U)
    return 1;

  const char name[] = "HEADLESS-1";
  gwipc_output_descriptor_upsert descriptor = {0};
  descriptor.struct_size = sizeof(descriptor);
  descriptor.output_id = 1;
  descriptor.kind = GWIPC_OUTPUT_HEADLESS;
  descriptor.capability_flags = GWIPC_OUTPUT_CAP_CONNECTED |
                                GWIPC_OUTPUT_CAP_ARBITRARY_HEADLESS_MODE |
                                GWIPC_OUTPUT_CAP_SCALE_CONFIGURABLE |
                                GWIPC_OUTPUT_CAP_TRANSFORM_CONFIGURABLE |
                                GWIPC_OUTPUT_CAP_PRIMARY_ELIGIBLE;
  descriptor.name = name;
  descriptor.name_length = strlen(name);
  descriptor.supported_transform_mask = GWIPC_OUTPUT_TRANSFORM_NORMAL;
  descriptor.minimum_scale_numerator = 1;
  descriptor.minimum_scale_denominator = 1;
  descriptor.maximum_scale_numerator = 4;
  descriptor.maximum_scale_denominator = 1;
  descriptor.maximum_scale_denominator_value =
      GWIPC_MAXIMUM_OUTPUT_SCALE_DENOMINATOR;
  descriptor.maximum_physical_width = GWIPC_MAXIMUM_OUTPUT_PHYSICAL_EXTENT;
  descriptor.maximum_physical_height = GWIPC_MAXIMUM_OUTPUT_PHYSICAL_EXTENT;

  gwipc_output_state_query query = {0};
  query.struct_size = sizeof(query);
  query.query_id = 1;
  query.flags = GWIPC_OUTPUT_QUERY_DESCRIPTORS | GWIPC_OUTPUT_QUERY_MODES |
                GWIPC_OUTPUT_QUERY_LAYOUT;
  gwipc_contract_payload *payload = NULL;
  if (gwipc_contract_encode_output_state_query(&query, &payload) !=
      GWIPC_STATUS_OK)
    return 1;
  gwipc_contract_payload_destroy(payload);
  return descriptor.output_id != 1;
}
