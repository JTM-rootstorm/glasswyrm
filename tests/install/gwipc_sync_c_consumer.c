#include <glasswyrm/ipc.h>

int main(void) {
  const gwipc_api_version api = gwipc_get_api_version();
  if (api.major != 0 || api.minor < 7 ||
      GWIPC_CAP_CPU_BUFFER_SYNCHRONIZATION != (UINT64_C(1) << 16) ||
      GWIPC_SYNCHRONIZATION_EVENTFD != 1)
    return 1;

  gwipc_buffer_attach attachment = {0};
  attachment.struct_size = sizeof(attachment);
  attachment.buffer_id = 1;
  attachment.surface_id = 2;
  attachment.width = attachment.height = 1;
  attachment.stride = 4;
  attachment.storage_size = 4;
  attachment.pixel_format = GWIPC_PIXEL_FORMAT_XRGB8888;
  attachment.alpha_semantics = GWIPC_ALPHA_OPAQUE;
  attachment.color.color_space = GWIPC_SDR_COLOR_SPACE_SRGB;
  attachment.color.transfer_function = GWIPC_TRANSFER_FUNCTION_SRGB;
  attachment.color.primaries = GWIPC_COLOR_PRIMARIES_SRGB;
  attachment.synchronization = GWIPC_SYNCHRONIZATION_EVENTFD;
  gwipc_contract_payload *payload = NULL;
  if (gwipc_contract_encode_buffer_attach(&attachment, &payload) !=
      GWIPC_STATUS_OK)
    return 1;
  gwipc_contract_payload_destroy(payload);
  return 0;
}
