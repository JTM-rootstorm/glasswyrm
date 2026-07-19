#pragma once

#include <cstdint>
#include <string>

namespace gw::test::m13 {

enum class EvidenceStage { Initial, Moved };

struct EvidenceHold {
  int milliseconds{};
  std::string ready_file;

  [[nodiscard]] bool valid() const noexcept {
    return milliseconds >= 1 && milliseconds <= 60'000 &&
           !ready_file.empty();
  }
  [[nodiscard]] bool disabled_or_valid() const noexcept {
    return (milliseconds == 0 && ready_file.empty()) || valid();
  }
};

void hold_for_evidence(const EvidenceHold& hold, std::uint32_t window,
                       EvidenceStage stage);
void self_test_evidence_hold();

}  // namespace gw::test::m13
