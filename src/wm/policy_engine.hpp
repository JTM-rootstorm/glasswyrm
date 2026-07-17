#ifndef GLASSWYRM_WM_POLICY_ENGINE_HPP
#define GLASSWYRM_WM_POLICY_ENGINE_HPP

#include "wm/types.hpp"
#include "wm/interactive_policy.hpp"

#include <array>

namespace glasswyrm::wm {

[[nodiscard]] Evaluation evaluate(const RawState& raw,
                                  std::uint64_t generation);
[[nodiscard]] std::array<std::uint8_t, 64> encode_policy_window_state(
    const WindowState& state) noexcept;
[[nodiscard]] std::uint64_t interactive_policy_hash(
    const PolicyState& policy, const InteractiveBindings& bindings) noexcept;

}  // namespace glasswyrm::wm

#endif
