#include "glasswyrmd/client_connection.hpp"

int main() {
  using namespace glasswyrm::server;
  RequestWorkBudget requests;
  for (std::size_t index = 0; index < kMaximumRequestsPerClientTurn; ++index) {
    if (!requests.available()) return 1;
    requests.record(4);
  }
  if (requests.available() ||
      requests.requests() != kMaximumRequestsPerClientTurn) {
    return 2;
  }

  RequestWorkBudget bytes;
  bytes.record(kMaximumRequestBytesPerClientTurn);
  if (bytes.available() || bytes.bytes() != kMaximumRequestBytesPerClientTurn) {
    return 3;
  }
  return 0;
}
