#include <glasswyrm/ipc.hpp>

#include <type_traits>

int main() {
  static_assert(!std::is_copy_constructible_v<glasswyrm::ipc::Listener>);
  static_assert(!std::is_copy_constructible_v<glasswyrm::ipc::Connection>);
  static_assert(!std::is_copy_constructible_v<glasswyrm::ipc::Message>);
  static_assert(!std::is_copy_constructible_v<glasswyrm::ipc::OwnedFd>);

  const gwipc_api_version api = gwipc_get_api_version();
  const gwipc_wire_version wire = gwipc_get_max_wire_version();
  gwipc_listener_options listener{};
  listener.struct_size = sizeof(listener);
  gwipc_connection_options connection{};
  connection.struct_size = sizeof(connection);
  glasswyrm::ipc::Listener listener_handle;
  glasswyrm::ipc::Connection connection_handle;

  gwipc_output_remove remove{sizeof(remove), 7, {}};
  gwipc_contract_payload* payload = nullptr;
  if (gwipc_contract_encode_output_remove(&remove, &payload) != GWIPC_STATUS_OK)
    return 1;
  gwipc_contract_payload_destroy(payload);

  return api.major != 0 || api.minor < 2 ||
         wire.major != 1 || wire.minor != 0 || listener.struct_size == 0 ||
         connection.struct_size == 0 || listener_handle || connection_handle;
}
