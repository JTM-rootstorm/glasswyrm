#pragma once

namespace glasswyrm::compositor {

[[nodiscard]] constexpr bool may_begin_vt_release(
    const bool requested, const bool producer_connected,
    const bool peer_validated, const bool producer_bootstrapped,
    const bool presentation_pending,
    const bool presentation_suspended) noexcept {
  return requested && producer_connected && peer_validated &&
         producer_bootstrapped && !presentation_pending &&
         !presentation_suspended;
}

[[nodiscard]] constexpr bool vt_release_blocks_contract_service(
    const bool requested, const bool producer_bootstrapped) noexcept {
  return requested && producer_bootstrapped;
}

}  // namespace glasswyrm::compositor
