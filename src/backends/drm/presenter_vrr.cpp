#include "backends/drm/presenter_vrr.hpp"

#include "output/vrr/reasons.hpp"

#include <utility>

namespace glasswyrm::drm {
namespace {

void add_reason(output::vrr::ReasonMask &mask,
                const output::vrr::Reason reason) noexcept {
  mask |= output::vrr::reason_bit(reason);
}

bool sequence_follows(const std::uint32_t previous,
                      const std::uint32_t current) noexcept {
  if (previous == 0 && current == 0)
    return true;
  const auto distance = static_cast<std::uint32_t>(current - previous);
  return distance != 0 && distance < (UINT32_C(1) << 31U);
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
  nominal_mode_interval_nanoseconds_ =
      output::vrr::refresh_interval_nanoseconds(refresh_millihertz);
  timing_statistics_.reset();
  if (nominal_mode_interval_nanoseconds_ != 0)
    timing_statistics_.emplace(nominal_mode_interval_nanoseconds_);
  transition_serial_available_ = false;
  last_transition_serial_ = 0;
  enabled_period_count_ = 0;
  disabled_period_count_ = 0;
  timestamp_unavailable_count_ = 0;
  reset_timing_period();
}

PresenterVrrPlan PresenterVrrState::plan(
    const output::VrrPresentationRequest &request,
    const bool explicit_reaffirmation) const {
  PresenterVrrPlan result;
  result.desired_enabled = request.valid && request.desired_enabled;
  if (request.valid &&
      (nominal_mode_interval_nanoseconds_ == 0 ||
       request.nominal_mode_interval_nanoseconds !=
           nominal_mode_interval_nanoseconds_)) {
    result.error =
        "VRR nominal mode interval does not match the selected DRM mode";
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
    const bool readback_enabled, const bool readback_valid,
    const std::uint64_t transition_serial) noexcept {
  effective_enabled_ = readback_valid && readback_enabled;
  property_readback_valid_ = readback_valid;
  reset_timing_period();
  transition_serial_available_ = transition_serial != 0;
  last_transition_serial_ = transition_serial;
  if (effective_enabled_)
    ++enabled_period_count_;
  else
    ++disabled_period_count_;
}

void PresenterVrrState::complete_flip(
    const bool desired_enabled, const bool readback_enabled,
    const bool readback_valid,
    const std::uint64_t transition_serial,
    const std::uint32_t sequence,
    const std::uint64_t kernel_timestamp_nanoseconds,
    const bool timestamp_available) noexcept {
  const auto previous_effective = effective_enabled_;
  effective_enabled_ = readback_valid && readback_enabled;
  property_readback_valid_ =
      readback_valid && readback_enabled == desired_enabled;
  const bool transition_changed =
      !transition_serial_available_ ||
      transition_serial != last_transition_serial_;
  if (effective_enabled_ != previous_effective || transition_changed) {
    reset_timing_period();
    if (effective_enabled_)
      ++enabled_period_count_;
    else
      ++disabled_period_count_;
  }
  transition_serial_available_ = true;
  last_transition_serial_ = transition_serial;

  flip_sequence_ = sequence;
  interval_nanoseconds_ = 0;
  timestamp_available_ = false;
  kernel_timestamp_nanoseconds_ = 0;

  const bool valid_timestamp = timestamp_available && timestamp_monotonic_ &&
                               kernel_timestamp_nanoseconds != 0;
  if (!valid_timestamp) {
    ++timestamp_unavailable_count_;
    reset_timing_period();
    flip_sequence_ = sequence;
    return;
  }

  if (timing_baseline_available_ &&
      (!sequence_follows(timing_baseline_sequence_, sequence) ||
       kernel_timestamp_nanoseconds <=
           timing_baseline_timestamp_nanoseconds_)) {
    ++timestamp_unavailable_count_;
    reset_timing_period();
    timing_baseline_available_ = true;
    timing_baseline_sequence_ = sequence;
    timing_baseline_timestamp_nanoseconds_ = kernel_timestamp_nanoseconds;
    flip_sequence_ = sequence;
    if (timing_statistics_)
      static_cast<void>(
          timing_statistics_->observe(sequence, kernel_timestamp_nanoseconds));
    return;
  }

  if (timing_baseline_available_)
    interval_nanoseconds_ =
        kernel_timestamp_nanoseconds - timing_baseline_timestamp_nanoseconds_;
  timing_baseline_available_ = true;
  timing_baseline_sequence_ = sequence;
  timing_baseline_timestamp_nanoseconds_ = kernel_timestamp_nanoseconds;
  flip_sequence_ = sequence;
  kernel_timestamp_nanoseconds_ = kernel_timestamp_nanoseconds;
  timestamp_available_ = true;
  if (timing_statistics_)
    static_cast<void>(
        timing_statistics_->observe(sequence, kernel_timestamp_nanoseconds));
}

void PresenterVrrState::reset_timing_period() noexcept {
  flip_sequence_ = 0;
  kernel_timestamp_nanoseconds_ = 0;
  interval_nanoseconds_ = 0;
  timestamp_available_ = false;
  timing_baseline_available_ = false;
  timing_baseline_sequence_ = 0;
  timing_baseline_timestamp_nanoseconds_ = 0;
  if (timing_statistics_)
    timing_statistics_->reset();
}

void PresenterVrrState::mark_suspended_off() noexcept {
  if (effective_enabled_)
    ++disabled_period_count_;
  effective_enabled_ = false;
  property_readback_valid_ = kms_state_.controllable;
  session_active_ = false;
  reset_timing_period();
}

void PresenterVrrState::mark_acquired_off() noexcept {
  effective_enabled_ = false;
  property_readback_valid_ = kms_state_.controllable;
  reset_timing_period();
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
  reset_timing_period();
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
