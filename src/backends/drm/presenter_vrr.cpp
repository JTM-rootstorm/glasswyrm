#include "backends/drm/presenter_vrr.hpp"

#include "output/vrr/reasons.hpp"

#include <utility>

namespace glasswyrm::drm {
namespace {

void add_reason(output::vrr::ReasonMask &mask,
                const output::vrr::Reason reason) noexcept {
  mask |= output::vrr::reason_bit(reason);
}

std::uint64_t target_interval(
    const std::uint32_t refresh_millihertz) noexcept {
  if (refresh_millihertz == 0)
    return 0;
  constexpr auto nanoseconds_per_millihertz =
      UINT64_C(1'000'000'000'000);
  return (nanoseconds_per_millihertz + refresh_millihertz / 2U) /
         refresh_millihertz;
}

} // namespace

void PresenterVrrState::initialize(const std::uint64_t output_id,
                                   KmsVrrState kms_state,
                                   const bool timestamp_monotonic,
                                   const std::uint32_t refresh_millihertz) noexcept {
  output_id_ = output_id;
  kms_state_ = std::move(kms_state);
  timestamp_monotonic_ = timestamp_monotonic;
  effective_enabled_ = false;
  property_readback_valid_ = false;
  session_active_ = true;
  flip_sequence_ = 0;
  kernel_timestamp_nanoseconds_ = 0;
  interval_nanoseconds_ = 0;
  target_interval_nanoseconds_ = target_interval(refresh_millihertz);
  timestamp_available_ = false;
  timing_statistics_.reset();
  if (target_interval_nanoseconds_ != 0)
    timing_statistics_.emplace(target_interval_nanoseconds_);
  enabled_period_count_ = 0;
  disabled_period_count_ = 0;
}

PresenterVrrPlan PresenterVrrState::plan(
    const output::VrrPresentationRequest &request,
    const bool explicit_reaffirmation) const {
  PresenterVrrPlan result;
  result.desired_enabled = request.valid && request.desired_enabled;
  result.requires_flip = true;
  if (request.valid &&
      (target_interval_nanoseconds_ == 0 ||
       request.target_interval_nanoseconds != target_interval_nanoseconds_)) {
    result.error =
        "VRR target interval does not match the selected DRM mode";
    return result;
  }
  if (result.desired_enabled &&
      request.decision != output::vrr::Decision::Enabled) {
    result.error = "VRR enable request does not carry an enabled decision";
    return result;
  }
  if (result.desired_enabled && !kms_state_.controllable) {
    result.error = "DRM VRR request is unsupported: " + kms_state_.diagnostic;
    return result;
  }
  result.accepted = true;
  result.include_property =
      kms_state_.controllable &&
      (result.desired_enabled != effective_enabled_ ||
       explicit_reaffirmation);
  return result;
}

void PresenterVrrState::complete_initial(
    const bool readback_enabled, const bool readback_valid) noexcept {
  effective_enabled_ = readback_valid && readback_enabled;
  property_readback_valid_ = readback_valid;
  if (effective_enabled_)
    ++enabled_period_count_;
  else
    ++disabled_period_count_;
}

void PresenterVrrState::complete_flip(
    const bool desired_enabled, const bool readback_enabled,
    const bool readback_valid,
    const std::uint32_t sequence,
    const std::uint64_t kernel_timestamp_nanoseconds,
    const bool timestamp_available) noexcept {
  const auto previous_effective = effective_enabled_;
  effective_enabled_ = readback_valid && readback_enabled;
  property_readback_valid_ =
      readback_valid && readback_enabled == desired_enabled;
  if (effective_enabled_ != previous_effective) {
    if (effective_enabled_)
      ++enabled_period_count_;
    else
      ++disabled_period_count_;
  }
  flip_sequence_ = sequence;
  interval_nanoseconds_ = 0;
  timestamp_available_ = timestamp_available && timestamp_monotonic_ &&
                         kernel_timestamp_nanoseconds != 0;
  if (timestamp_available_ && kernel_timestamp_nanoseconds_ != 0 &&
      kernel_timestamp_nanoseconds > kernel_timestamp_nanoseconds_)
    interval_nanoseconds_ =
        kernel_timestamp_nanoseconds - kernel_timestamp_nanoseconds_;
  kernel_timestamp_nanoseconds_ =
      timestamp_available_ ? kernel_timestamp_nanoseconds : 0;
  if (timing_statistics_)
    static_cast<void>(timing_statistics_->observe(
        sequence, kernel_timestamp_nanoseconds, timestamp_available_));
}

void PresenterVrrState::mark_suspended_off() noexcept {
  if (effective_enabled_)
    ++disabled_period_count_;
  effective_enabled_ = false;
  property_readback_valid_ = kms_state_.controllable;
  session_active_ = false;
  timestamp_available_ = false;
  interval_nanoseconds_ = 0;
}

void PresenterVrrState::mark_acquired_off() noexcept {
  effective_enabled_ = false;
  property_readback_valid_ = kms_state_.controllable;
  timestamp_available_ = false;
  interval_nanoseconds_ = 0;
}

void PresenterVrrState::mark_session_active() noexcept {
  session_active_ = true;
}

void PresenterVrrState::mark_restored() noexcept {
  if (effective_enabled_ != kms_state_.original_enabled) {
    if (kms_state_.original_enabled)
      ++enabled_period_count_;
    else
      ++disabled_period_count_;
  }
  effective_enabled_ = kms_state_.original_enabled;
  property_readback_valid_ = kms_state_.crtc_property_present;
  session_active_ = false;
  timestamp_available_ = false;
  interval_nanoseconds_ = 0;
}

output::VrrPresentationCapability PresenterVrrState::capability(
    const bool output_enabled, const bool connected) const noexcept {
  output::VrrPresentationCapability result;
  result.output_enabled = output_enabled;
  result.connected = connected;
  result.drm = true;
  result.connector_property_present = kms_state_.connector_property_present;
  result.hardware_capable = kms_state_.hardware_capable;
  result.atomic_kms_available = kms_state_.atomic_available;
  result.atomic_test_passed =
      kms_state_.test_off_passed && kms_state_.test_on_passed;
  result.kms_controllable = kms_state_.controllable;
  result.atomic_required = true;
  result.session_active = session_active_;
  result.suspended = !session_active_;
  result.timing_available = timestamp_monotonic_;
  if (!output_enabled)
    add_reason(result.reason_flags, output::vrr::Reason::OutputDisabled);
  if (!connected)
    add_reason(result.reason_flags, output::vrr::Reason::OutputNotConnected);
  if (!kms_state_.hardware_capable)
    add_reason(result.reason_flags, output::vrr::Reason::OutputNotVrrCapable);
  if (!kms_state_.atomic_available)
    add_reason(result.reason_flags, output::vrr::Reason::AtomicKmsUnavailable);
  if (!kms_state_.crtc_property_present)
    add_reason(result.reason_flags, output::vrr::Reason::VrrPropertyMissing);
  if (kms_state_.hardware_capable && kms_state_.crtc_property_present &&
      !result.atomic_test_passed)
    add_reason(result.reason_flags, output::vrr::Reason::VrrAtomicTestFailed);
  if (!session_active_) {
    add_reason(result.reason_flags, output::vrr::Reason::SessionInactive);
    add_reason(result.reason_flags, output::vrr::Reason::VtSuspended);
  }
  if (!timestamp_monotonic_)
    add_reason(result.reason_flags, output::vrr::Reason::TimingUnavailable);
  return result;
}

output::VrrPresentationFeedback PresenterVrrState::feedback() const noexcept {
  return {output_id_,
          effective_enabled_,
          property_readback_valid_,
          session_active_,
          flip_sequence_,
          0,
          kernel_timestamp_nanoseconds_,
          interval_nanoseconds_,
          timestamp_available_};
}

output::vrr::TimingSummary PresenterVrrState::timing_summary() const noexcept {
  return timing_statistics_ ? timing_statistics_->summary()
                            : output::vrr::TimingSummary{};
}

} // namespace glasswyrm::drm
