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

  if (api.major != 0 || api.minor != 1 || api.patch != 0 || wire.major != 1 ||
      wire.minor != 0 || listener.struct_size == 0 ||
      connection.struct_size == 0) {
    return 1;
  }
  return gwipc_status_string(GWIPC_STATUS_OK) == NULL;
}
