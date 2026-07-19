#pragma once

#include <glasswyrm/ipc.h>

#include <cstdint>

namespace glasswyrm::compositor {

enum class SessionWaitMessageRoute {
  Acknowledgement,
  DrainContract,
};

[[nodiscard]] constexpr SessionWaitMessageRoute session_wait_message_route(
    const std::uint16_t type) noexcept {
  return type == GWIPC_MESSAGE_SESSION_STATE_ACKNOWLEDGED
             ? SessionWaitMessageRoute::Acknowledgement
             : SessionWaitMessageRoute::DrainContract;
}

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
