#ifndef GLASSWYRM_WM_POLICY_ENGINE_INTERNAL_HPP
#define GLASSWYRM_WM_POLICY_ENGINE_INTERNAL_HPP

#include "wm/types.hpp"

#include <cstdint>
#include <vector>

namespace glasswyrm::wm::detail {

inline constexpr std::uint32_t kAboveFlag = 1U << 0;
inline constexpr std::uint32_t kBypassCompositorFlag = 1U << 1;
inline constexpr std::uint32_t kInputDisabledFlag = 1U << 2;
inline constexpr std::uint32_t kKnownWindowFlags =
    kAboveFlag | kBypassCompositorFlag | kInputDisabledFlag;

[[nodiscard]] EvaluationError validate(const RawState& raw);
void assign_outputs(const RawState& raw, PolicyState& policy);
void apply_placement_and_fullscreen_geometry(const RawState& raw,
                                             PolicyState& policy);
void apply_multi_output_geometry(const RawState& raw, PolicyState& policy);
[[nodiscard]] std::vector<std::uint32_t> transient_parent_first(
    const RawState& raw);
void apply_stacking_and_transient_ordering(const RawState& raw,
                                           PolicyState& policy);
void apply_focus_and_visibility(const RawState& raw, PolicyState& policy);
[[nodiscard]] std::uint64_t policy_hash(const PolicyState& policy) noexcept;

}  // namespace glasswyrm::wm::detail

#endif
