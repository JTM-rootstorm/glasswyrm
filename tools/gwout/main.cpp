#include <iostream>

#include <glasswyrm/backends/headless.hpp>

int main() {
  const glasswyrm::backends::HeadlessBackend backend;
  std::cout << "gwout scaffold: " << backend.describe() << '\n';
  return 0;
}
