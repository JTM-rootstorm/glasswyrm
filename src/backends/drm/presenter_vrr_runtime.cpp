#include "backends/drm/presenter.hpp"

#include <algorithm>
#include <array>

namespace glasswyrm::drm {

std::optional<output::VrrPresentationCapability>
DrmPresenter::vrr_capability(const std::uint64_t output_id) const noexcept {
  if (!vrr_contract_enabled_ || !vrr_state_initialized_ || !initialized_ ||
      output_id == 0 ||
      (config_.output.output_id != 0 && output_id != config_.output.output_id))
    return std::nullopt;
  const auto connector = std::ranges::find_if(
      device_.snapshot().connectors,
      [&](const Connector& value) { return value.id == pipeline_.connector; });
  const bool connected = connector != device_.snapshot().connectors.end() &&
                         connector->status == ConnectionStatus::Connected;
  return vrr_state_.capability(true, connected);
}

bool DrmPresenter::configure_vrr_contract(const bool enabled,
                                          std::string& error) {
  error.clear();
  if (!initialized_ || shutdown_ || fatal_ || pending_) {
    error = "DRM VRR contract cannot change in the current presenter state";
    return false;
  }
  if (!enabled) {
    const bool was_effective =
        vrr_contract_enabled_ && vrr_state_initialized_ &&
        vrr_state_.effective_enabled();
    if (vrr_contract_enabled_ && !set_vrr_off_on_current_frame(error))
      return false;
    if (was_effective)
      vrr_state_.complete_initial(false,
                                  vrr_state_.kms_state().controllable);
    vrr_contract_enabled_ = false;
    return true;
  }
  if (!initialize_vrr_controller(error)) return false;
  vrr_contract_enabled_ = true;
  if (vrr_report_ && !vrr_capability_reported_) {
    if (!append_vrr_capability_report(error)) {
      vrr_contract_enabled_ = false;
      return false;
    }
    vrr_capability_reported_ = true;
  }
  return true;
}

bool DrmPresenter::activate_session(std::string& error) {
  if (!initialized_ || shutdown_ || fatal_ || suspended_) {
    error = "DRM presentation session cannot become active";
    return false;
  }
  if (vrr_contract_enabled_ && vrr_state_initialized_)
    vrr_state_.mark_session_active();
  error.clear();
  return true;
}

bool DrmPresenter::blocking_modeset(DumbBuffer& buffer, std::string& error) {
  if (selected_api_ == ReportApiPath::Atomic) {
    const auto request = atomic_initial_request(
        pipeline_, saved_.properties, mode_blob_.id(), buffer.framebuffer_id(),
        config_.output.width, config_.output.height,
        vrr_contract_enabled_ && vrr_state_initialized_ &&
            vrr_state_.kms_state().controllable);
    return kms_.atomic_commit(device_.borrowed_kms_fd(), request,
                              AtomicAllowModeset, nullptr, error);
  }
  const KmsCrtcState state{pipeline_.crtc, buffer.framebuffer_id(), 0, 0, true,
                           kms_mode_};
  const std::array connector{pipeline_.connector};
  return kms_.legacy_set_crtc(device_.borrowed_kms_fd(), state, connector,
                              error);
}

bool DrmPresenter::verify_vrr_state(const bool expected,
                                    std::string& error) {
  if (!vrr_contract_enabled_ || !vrr_state_initialized_ ||
      !vrr_state_.kms_state().controllable) {
    error.clear();
    return true;
  }
  return verify_kms_vrr_enabled(kms_, device_.borrowed_kms_fd(), pipeline_,
                                vrr_state_.kms_state(), expected, error);
}

bool DrmPresenter::set_vrr_off_on_current_frame(std::string& error) {
  if (!vrr_contract_enabled_ || !vrr_state_initialized_ ||
      !vrr_state_.kms_state().controllable ||
      !vrr_state_.effective_enabled()) {
    error.clear();
    return true;
  }
  return set_vrr_off_on_frame(buffers_.front().framebuffer_id(), error);
}

bool DrmPresenter::set_vrr_off_on_frame(const std::uint32_t framebuffer_id,
                                        std::string& error) {
  if (!vrr_state_initialized_ || !vrr_state_.kms_state().controllable) {
    error.clear();
    return true;
  }
  auto request =
      atomic_flip_request(pipeline_, saved_.properties, framebuffer_id);
  request = make_vrr_atomic_request(request, vrr_state_.kms_state(), false);
  if (!kms_.atomic_commit(device_.borrowed_kms_fd(), request, 0, nullptr,
                          error))
    return false;
  return verify_vrr_state(false, error);
}

bool DrmPresenter::append_vrr_report(const DrmVrrReportRecord& record,
                                     std::string& error) {
  StagedDrmReport staged;
  if (!vrr_report_) {
    error.clear();
    return true;
  }
  return vrr_report_->stage(DrmReportRecord{record}, staged, error) &&
         vrr_report_->commit(staged, error);
}

DrmVrrDecisionReport DrmPresenter::vrr_decision_report(
    const output::VrrPresentationRequest& request,
    const std::uint64_t commit_id, const std::uint64_t generation,
    const bool effective) const {
  return {commit_id,
          generation,
          config_.output.output_id,
          request.requested_mode,
          request.candidate_window_id,
          request.candidate_surface_id,
          request.desired_enabled,
          effective,
          request.reason_flags,
          vrr_state_.session_active(),
          request.transition_serial};
}

DrmVrrTimingReport DrmPresenter::vrr_timing_report(
    const std::uint64_t commit_id, const std::uint64_t generation,
    const output::VrrPresentationRequest& request,
    const output::VrrPresentationFeedback& feedback) const {
  const auto target = request.target_interval_nanoseconds;
  const auto interval = feedback.interval_nanoseconds;
  const auto distance = interval >= target ? interval - target
                                            : target - interval;
  return {commit_id,
          generation,
          feedback.flip_sequence,
          feedback.kernel_timestamp_nanoseconds,
          interval,
          target,
          feedback.effective_enabled,
          feedback.timestamp_available && target != 0 &&
              distance <= output::vrr::timing_tolerance(target)};
}

}  // namespace glasswyrm::drm
