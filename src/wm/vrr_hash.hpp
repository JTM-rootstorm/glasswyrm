#ifndef GLASSWYRM_WM_VRR_HASH_HPP
#define GLASSWYRM_WM_VRR_HASH_HPP

#include "wm/vrr_policy.hpp"

namespace glasswyrm::wm {

[[nodiscard]] std::uint64_t vrr_policy_hash(
    const PolicyState& base, const VrrInputs& inputs,
    const VrrPolicyState& policy) noexcept;

}  // namespace glasswyrm::wm

#endif
