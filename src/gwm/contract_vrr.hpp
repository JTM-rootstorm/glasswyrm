#ifndef GLASSWYRM_GWM_CONTRACT_VRR_HPP
#define GLASSWYRM_GWM_CONTRACT_VRR_HPP

#include "gwm/contract_dispatch.hpp"

#include <glasswyrm/ipc.h>

#include <cstddef>

namespace glasswyrm::wm::runtime {

[[nodiscard]] bool negotiated_vrr_profile(
    const gwipc_connection* connection) noexcept;

[[nodiscard]] gwipc_policy_result vrr_result_from(
    VrrEvaluationError error) noexcept;

[[nodiscard]] bool consume_vrr_contract(PeerState& peer,
                                        const gwipc_connection* connection,
                                        const gwipc_decoded_contract* contract,
                                        std::uint16_t type);

[[nodiscard]] bool populate_vrr_memberships(const RawState& raw,
                                            VrrInputs& inputs);

[[nodiscard]] bool preflight_vrr_policy(const VrrPolicyState& policy,
                                        std::size_t& queued_bytes);

[[nodiscard]] bool enqueue_vrr_policy(gwipc_connection* connection,
                                      const VrrPolicyState& policy);

}  // namespace glasswyrm::wm::runtime

#endif
