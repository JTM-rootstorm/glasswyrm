#ifndef GLASSWYRM_WM_VRR_VALIDATION_HPP
#define GLASSWYRM_WM_VRR_VALIDATION_HPP

#include "wm/vrr_policy.hpp"

namespace glasswyrm::wm {

[[nodiscard]] VrrEvaluationError validate_vrr_inputs(
    const RawState& raw, const PolicyState& base,
    const VrrInputs& inputs) noexcept;

}  // namespace glasswyrm::wm

#endif
