#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace glasswyrm::output::vrr {

enum class Reason : std::uint8_t {
  OutputDisabled = 0,
  OutputNotConnected,
  OutputNotDrm,
  OutputNotVrrCapable,
  AtomicKmsUnavailable,
  VrrPropertyMissing,
  VrrAtomicTestFailed,
  SessionInactive,
  VtSuspended,
  OutputConfigurationBusy,
  PolicyOff,
  NoCandidate,
  WindowMissing,
  WindowHidden,
  WindowUnmanaged,
  WindowUnfocused,
  WindowNotFullscreen,
  WindowNotBorderlessFullscreen,
  WindowSpansOutputs,
  WindowPreferenceDisabled,
  WindowDidNotRequest,
  SurfaceMissing,
  SurfaceMetadataOnly,
  SurfaceNotVisible,
  SurfaceNotOpaque,
  SurfaceOnWrongOutput,
  SurfaceMembershipInvalid,
  PresenterRejected,
  PropertyReadbackMismatch,
  TimingUnavailable,
  HardwareBehaviorUnconfirmed,
  SimulatedHeadless,
  ManualAlwaysEligible,
};

using ReasonMask = std::uint64_t;

inline constexpr std::size_t kReasonCount = 33;
static_assert(static_cast<std::uint8_t>(Reason::ManualAlwaysEligible) ==
              kReasonCount - 1);
inline constexpr ReasonMask kKnownReasonMask =
    (UINT64_C(1) << kReasonCount) - UINT64_C(1);

[[nodiscard]] constexpr ReasonMask reason_bit(const Reason reason) noexcept {
  return UINT64_C(1) << static_cast<std::uint8_t>(reason);
}

[[nodiscard]] constexpr bool has_reason(const ReasonMask mask,
                                        const Reason reason) noexcept {
  return (mask & reason_bit(reason)) != 0;
}

[[nodiscard]] std::optional<Reason> primary_reason(ReasonMask mask) noexcept;
[[nodiscard]] std::string_view reason_name(Reason reason) noexcept;
[[nodiscard]] bool valid_reason_mask(ReasonMask mask) noexcept;
[[nodiscard]] const std::array<Reason, kReasonCount> &
reason_precedence() noexcept;

} // namespace glasswyrm::output::vrr
