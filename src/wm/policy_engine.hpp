#ifndef GLASSWYRM_WM_POLICY_ENGINE_HPP
#define GLASSWYRM_WM_POLICY_ENGINE_HPP

#include "wm/types.hpp"

namespace glasswyrm::wm {

[[nodiscard]] Evaluation evaluate(const RawState& raw,
                                  std::uint64_t generation);

}  // namespace glasswyrm::wm

#endif
