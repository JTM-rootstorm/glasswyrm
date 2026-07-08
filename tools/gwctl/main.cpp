#include <iostream>

#include <glasswyrm/core/server.hpp>

int main() {
  const glasswyrm::core::Server server;
  std::cout << "gwctl scaffold: " << server.describe() << '\n';
  return 0;
}
