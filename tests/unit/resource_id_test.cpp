#include "glasswyrmd/resource_id.hpp"

int main() {
  using glasswyrm::server::is_server_owned_id;
  using glasswyrm::server::resource_id_matches_client;

  constexpr std::uint32_t base = 0x00400000;
  constexpr std::uint32_t mask = 0x001fffff;
  static_assert(resource_id_matches_client(base, base, mask));
  static_assert(resource_id_matches_client(base | mask, base, mask));
  static_assert(!resource_id_matches_client(0, base, mask));
  static_assert(!resource_id_matches_client(0x00800001, base, mask));
  static_assert(!resource_id_matches_client(base | 1, base | 1, mask));
  static_assert(is_server_owned_id(1));
  static_assert(is_server_owned_id(2));
  static_assert(is_server_owned_id(3));
  static_assert(!is_server_owned_id(base));
  return 0;
}
