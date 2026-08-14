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

  RequestWorkBudget semantic_work;
  semantic_work.record(40);
  semantic_work.record_semantic_work(
      kMaximumSemanticWorkBytesPerClientTurn);
  if (semantic_work.available() || semantic_work.requests() != 1 ||
      semantic_work.bytes() != 40 ||
      semantic_work.semantic_work_bytes() !=
          kMaximumSemanticWorkBytesPerClientTurn) {
    return 4;
  }

  RequestWorkBudget overflow;
  overflow.record_semantic_work(std::numeric_limits<std::size_t>::max());
  overflow.record_semantic_work(1);
  if (overflow.semantic_work_bytes() !=
      std::numeric_limits<std::size_t>::max()) {
    return 5;
  }
  return 0;
}
