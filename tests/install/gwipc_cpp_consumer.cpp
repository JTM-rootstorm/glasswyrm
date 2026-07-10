#include <glasswyrm/ipc.hpp>

#include <type_traits>

int main() {
  static_assert(!std::is_copy_constructible_v<glasswyrm::ipc::Listener>);
  static_assert(!std::is_copy_constructible_v<glasswyrm::ipc::Connection>);
  static_assert(!std::is_copy_constructible_v<glasswyrm::ipc::Message>);
  static_assert(!std::is_copy_constructible_v<glasswyrm::ipc::OwnedFd>);

  const gwipc_api_version api = gwipc_get_api_version();
  const gwipc_wire_version wire = gwipc_get_max_wire_version();
  const gwipc_listener_options listener{sizeof(gwipc_listener_options)};
  const gwipc_connection_options connection{sizeof(gwipc_connection_options)};
  glasswyrm::ipc::Listener listener_handle;
  glasswyrm::ipc::Connection connection_handle;

  return api.major != 0 || api.minor != 1 || api.patch != 0 ||
         wire.major != 1 || wire.minor != 0 || listener.struct_size == 0 ||
         connection.struct_size == 0 || listener_handle || connection_handle;
}
