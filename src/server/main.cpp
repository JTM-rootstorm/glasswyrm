#include <iostream>

#include <glasswyrm/core/server.hpp>
#include <glasswyrm/protocol/x11.hpp>

int main() {
  const glasswyrm::core::Server server;
  const auto protocol = glasswyrm::protocol::protocol_identity();

  std::cout << server.describe() << '\n';
  std::cout << "protocol: " << protocol.name << " ("
            << glasswyrm::protocol::compatibility_tier_name(protocol.tier)
            << ")\n";
  std::cout << "note: no X11 socket is opened by the scaffold binary\n";

  return 0;
}
