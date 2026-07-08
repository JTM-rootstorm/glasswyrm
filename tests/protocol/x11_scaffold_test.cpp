#include <cassert>

#include <glasswyrm/protocol/x11.hpp>

int main() {
  assert(glasswyrm::protocol::is_valid_setup_byte_order('l'));
  assert(glasswyrm::protocol::is_valid_setup_byte_order('B'));
  assert(!glasswyrm::protocol::is_valid_setup_byte_order('x'));

  const auto identity = glasswyrm::protocol::protocol_identity();
  assert(identity.tier == glasswyrm::protocol::CompatibilityTier::ToyClients);
  assert(!identity.core_x11_enabled);
  assert(!identity.tcp_listening_enabled);

  return 0;
}
