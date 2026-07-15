#include <glasswyrm/ipc.hpp>

#include <type_traits>

int main() {
  static_assert(!std::is_copy_constructible_v<glasswyrm::ipc::Listener>);
  static_assert(!std::is_copy_constructible_v<glasswyrm::ipc::Connection>);
  static_assert(!std::is_copy_constructible_v<glasswyrm::ipc::Message>);
  static_assert(!std::is_copy_constructible_v<glasswyrm::ipc::OwnedFd>);

  const auto api = gwipc_get_api_version();
  const auto wire = gwipc_get_max_wire_version();
  glasswyrm::ipc::Listener listener;
  glasswyrm::ipc::Connection connection;
  return api.major != 0 || api.minor < 1 || wire.major != 1 ||
         wire.minor != 0 || listener || connection;
}
