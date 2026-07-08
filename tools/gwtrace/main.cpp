#include <iostream>

#include <glasswyrm/protocol/x11.hpp>

int main() {
  const auto protocol = glasswyrm::protocol::protocol_identity();
  std::cout << "gwtrace scaffold: " << protocol.name << '\n';
  std::cout << "tcp listening: "
            << (protocol.tcp_listening_enabled ? "enabled" : "disabled")
            << '\n';
  return 0;
}
