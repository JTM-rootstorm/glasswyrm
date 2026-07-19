#include "output/vrr/reasons.hpp"

namespace glasswyrm::output::vrr {
namespace {

constexpr std::array kPrecedence{
    Reason::OutputDisabled,
    Reason::OutputNotConnected,
    Reason::OutputNotDrm,
    Reason::OutputNotVrrCapable,
    Reason::AtomicKmsUnavailable,
    Reason::VrrPropertyMissing,
    Reason::VrrAtomicTestFailed,
    Reason::SessionInactive,
    Reason::VtSuspended,
    Reason::OutputConfigurationBusy,
    Reason::PresenterRejected,
    Reason::PropertyReadbackMismatch,
    Reason::TimingUnavailable,
    Reason::HardwareBehaviorUnconfirmed,
    Reason::PolicyOff,
    Reason::NoCandidate,
    Reason::WindowMissing,
    Reason::WindowHidden,
    Reason::WindowUnmanaged,
    Reason::WindowUnfocused,
    Reason::WindowNotFullscreen,
    Reason::WindowNotBorderlessFullscreen,
    Reason::WindowSpansOutputs,
    Reason::WindowPreferenceDisabled,
    Reason::WindowDidNotRequest,
    Reason::SurfaceMissing,
    Reason::SurfaceMetadataOnly,
    Reason::SurfaceNotVisible,
    Reason::SurfaceNotOpaque,
    Reason::SurfaceOnWrongOutput,
    Reason::SurfaceMembershipInvalid,
    Reason::SimulatedHeadless,
    Reason::ManualAlwaysEligible,
};

constexpr std::array<std::string_view, kReasonCount> kNames{
    "OutputDisabled",
    "OutputNotConnected",
    "OutputNotDrm",
    "OutputNotVrrCapable",
    "AtomicKmsUnavailable",
    "VrrPropertyMissing",
    "VrrAtomicTestFailed",
    "SessionInactive",
    "VtSuspended",
    "OutputConfigurationBusy",
    "PolicyOff",
    "NoCandidate",
    "WindowMissing",
    "WindowHidden",
    "WindowUnmanaged",
    "WindowUnfocused",
    "WindowNotFullscreen",
    "WindowNotBorderlessFullscreen",
    "WindowSpansOutputs",
    "WindowPreferenceDisabled",
    "WindowDidNotRequest",
    "SurfaceMissing",
    "SurfaceMetadataOnly",
    "SurfaceNotVisible",
    "SurfaceNotOpaque",
    "SurfaceOnWrongOutput",
    "SurfaceMembershipInvalid",
    "PresenterRejected",
    "PropertyReadbackMismatch",
    "TimingUnavailable",
    "HardwareBehaviorUnconfirmed",
    "SimulatedHeadless",
    "ManualAlwaysEligible",
};

static_assert(kPrecedence.size() == kReasonCount);
static_assert(kNames.size() == kReasonCount);

} // namespace

std::optional<Reason> primary_reason(const ReasonMask mask) noexcept {
  for (const auto reason : kPrecedence) {
    if (has_reason(mask, reason))
      return reason;
  }
  return std::nullopt;
}

std::string_view reason_name(const Reason reason) noexcept {
  const auto index = static_cast<std::size_t>(reason);
  return index < kNames.size() ? kNames[index] : std::string_view{};
}

bool valid_reason_mask(const ReasonMask mask) noexcept {
  return (mask & ~kKnownReasonMask) == 0;
}

const std::array<Reason, kReasonCount> &reason_precedence() noexcept {
  return kPrecedence;
}

} // namespace glasswyrm::output::vrr
