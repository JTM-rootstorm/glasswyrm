#ifndef GLASSWYRM_WM_VRR_POLICY_HPP
#define GLASSWYRM_WM_VRR_POLICY_HPP

#include "wm/types.hpp"

#include <cstdint>
#include <map>
#include <vector>

namespace glasswyrm::wm {

enum class VrrPolicyMode : std::uint16_t {
  Off = 1,
  Fullscreen,
  Focused,
  AppRequested,
  AlwaysEligible,
};

enum class VrrWindowPreference : std::uint16_t {
  Default = 0,
  Disable,
  Allow,
  Prefer,
};

namespace vrr_reason {
inline constexpr std::uint64_t output_disabled = UINT64_C(1) << 0U;
inline constexpr std::uint64_t output_not_vrr_capable = UINT64_C(1) << 3U;
inline constexpr std::uint64_t atomic_kms_unavailable = UINT64_C(1) << 4U;
inline constexpr std::uint64_t policy_off = UINT64_C(1) << 10U;
inline constexpr std::uint64_t no_candidate = UINT64_C(1) << 11U;
inline constexpr std::uint64_t window_hidden = UINT64_C(1) << 13U;
inline constexpr std::uint64_t window_unmanaged = UINT64_C(1) << 14U;
inline constexpr std::uint64_t window_unfocused = UINT64_C(1) << 15U;
inline constexpr std::uint64_t window_not_fullscreen = UINT64_C(1) << 16U;
inline constexpr std::uint64_t window_not_borderless_fullscreen =
    UINT64_C(1) << 17U;
inline constexpr std::uint64_t window_spans_outputs = UINT64_C(1) << 18U;
inline constexpr std::uint64_t window_preference_disabled =
    UINT64_C(1) << 19U;
inline constexpr std::uint64_t window_did_not_request = UINT64_C(1) << 20U;
inline constexpr std::uint64_t surface_membership_invalid =
    UINT64_C(1) << 26U;
inline constexpr std::uint64_t manual_always_eligible = UINT64_C(1) << 32U;
}  // namespace vrr_reason

struct VrrOutputInput {
  std::uint64_t output_id{};
  VrrPolicyMode mode{VrrPolicyMode::Off};
  bool hardware_capable{};
  bool kms_controllable{};
  std::uint32_t flags{};
};

struct VrrWindowInput {
  std::uint32_t window_id{};
  VrrWindowPreference preference{VrrWindowPreference::Default};
  std::vector<std::uint64_t> output_membership;
  std::uint32_t flags{};
};

struct VrrInputs {
  bool complete{};
  std::map<std::uint64_t, VrrOutputInput> outputs;
  std::map<std::uint32_t, VrrWindowInput> windows;
};

struct VrrWindowState {
  std::uint32_t window_id{};
  std::uint64_t output_id{};
  VrrWindowPreference preference{VrrWindowPreference::Default};
  bool selected{};
  bool eligible{};
  bool visible{};
  bool focused{};
  bool fullscreen{};
  bool borderless_fullscreen{};
  bool exclusive_output_membership{};
  std::uint64_t reason_flags{};
  std::uint32_t flags{};
};

struct VrrOutputState {
  std::uint64_t output_id{};
  VrrPolicyMode mode{VrrPolicyMode::Off};
  std::uint32_t selected_window_id{};
  bool desired_enabled{};
  bool candidate_required{};
  std::uint64_t reason_flags{};
  std::uint32_t flags{};
};

struct VrrPolicyState {
  std::uint64_t generation{};
  std::uint64_t base_policy_hash{};
  std::uint64_t hash{};
  std::map<std::uint64_t, VrrOutputState> outputs;
  std::map<std::uint32_t, VrrWindowState> windows;
};

enum class VrrEvaluationError : std::uint8_t {
  None,
  IncompleteSnapshot,
  BasePolicyMismatch,
  InvalidOutput,
  InvalidWindow,
  UnknownReference,
  Limit,
};

struct VrrEvaluation {
  VrrEvaluationError error{VrrEvaluationError::None};
  VrrPolicyState policy;
  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VrrEvaluationError::None;
  }
};

[[nodiscard]] bool classify_borderless_fullscreen(
    const RawState& raw, const PolicyState& base, const VrrWindowInput& input,
    std::uint32_t window_id) noexcept;

[[nodiscard]] VrrEvaluation evaluate_vrr_policy(const RawState& raw,
                                                const PolicyState& base,
                                                const VrrInputs& inputs);

}  // namespace glasswyrm::wm

#endif
