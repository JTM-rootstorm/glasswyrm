#include "backends/drm/presenter.hpp"

#include <string>

namespace glasswyrm::drm {

output::BackendStateResult DrmPresenter::suspend(std::string& error) {
  if (pending_) {
    error = "cannot release the VT while a page flip remains pending";
    record_fatal("vt-release", error);
    fatal_ = true;
    return output::BackendStateResult::Fatal;
  }
  if (!set_vrr_off_on_current_frame(error)) {
    record_fatal("vt-release-vrr-off", error);
    fatal_ = true;
    return output::BackendStateResult::Fatal;
  }
  if (direct_session_ && !direct_session_->release(error)) {
    record_fatal("vt-release", error);
    fatal_ = true;
    return output::BackendStateResult::Fatal;
  }
  suspended_ = true;
  if (vrr_state_initialized_) vrr_state_.mark_suspended_off();
  if (config_.damage_aware_copy && damage_history_) {
    damage_history_->clear();
    buffers_.invalidate_content();
  }
  if (direct_session_) {
    const VtReport record{VtTransition::Release, false, false, committed_hash_};
    if (!append_report(record, error)) return output::BackendStateResult::Fatal;
  }
  return output::BackendStateResult::Complete;
}

output::PresentResult DrmPresenter::resume(
    const output::SoftwareFrameView& committed) {
  if (!suspended_ || committed_pixels_.empty() ||
      output::hash_visible_xrgb8888(committed.pixels) != committed_hash_)
    return {output::PresentDisposition::Fatal, 0, 0,
            "DRM resume frame does not match the committed presentation"};
  std::string error;
  const bool success = direct_session_ ? direct_session_->reacquire(error)
                                       : present_committed_frame(error);
  if (!success) {
    record_fatal("vt-acquire", error);
    fatal_ = true;
    return {output::PresentDisposition::Fatal, 0, 0, error};
  }
  suspended_ = false;
  return {output::PresentDisposition::Complete, 0, committed_hash_, {}};
}

bool DrmPresenter::quiesce_pending_flip(std::string& error) {
  if (!pending_) {
    error.clear();
    return true;
  }
  error = "page flip did not quiesce within " +
          std::to_string(kPageFlipTimeoutMilliseconds) + " milliseconds";
  return false;
}

bool DrmPresenter::acquire_master(std::string& error) {
  if (!device_.may_manage_master()) {
    error = "external DRM sessions cannot acquire master";
    return false;
  }
  if (master_owned_) {
    error.clear();
    return true;
  }
  if (!kms_.acquire_master(device_.borrowed_kms_fd(), error)) return false;
  master_owned_ = true;
  return true;
}

bool DrmPresenter::drop_master(std::string& error) {
  if (!device_.may_manage_master()) {
    error = "external DRM sessions cannot drop master";
    return false;
  }
  if (!master_owned_) {
    error.clear();
    return true;
  }
  if (!kms_.drop_master(device_.borrowed_kms_fd(), error)) return false;
  master_owned_ = false;
  return true;
}

bool DrmPresenter::present_committed_frame(std::string& error) {
  if (committed_pixels_.empty()) {
    error = "no committed DRM frame is available for re-modeset";
    return false;
  }
  auto& target = buffers_.back();
  const output::SoftwareFrameView frame{
      config_.output, committed_pixels_, {}, 0, committed_generation_, 0};
  DamageCopyPlan damage_copy;
  if (!copy_frame_to(target, frame, committed_hash_,
                     FullCopyReason::VirtualTerminalResume, damage_copy,
                     error) ||
      target.visible_hash() != committed_hash_) {
    if (error.empty())
      error = "re-modeset scanout hash differs from committed frame";
    return false;
  }
  const auto next_front_index = static_cast<std::uint32_t>(1U - front_index_);
  const auto damage_report = damage_copy_report(
      target, damage_copy, committed_generation_, next_front_index);
  if (!blocking_modeset(target, error) || !verify_vrr_state(false, error))
    return false;
  if (vrr_state_initialized_) vrr_state_.mark_acquired_off();
  complete_damage_copy(target, damage_copy, committed_generation_);
  buffers_.promote_back();
  front_index_ = 1U - front_index_;
  display_taken_ = true;
  if (!direct_session_) return true;
  if (config_.damage_aware_copy && !append_report(damage_report, error))
    return false;
  const VtReport record{VtTransition::Acquire, true, true, committed_hash_};
  return append_report(record, error);
}

bool DrmPresenter::restore_original_display(std::string& error) {
  if (!display_taken_) {
    error.clear();
    return true;
  }
  if (!restore_saved_state(kms_, device_.borrowed_kms_fd(), saved_, error))
    return false;
  if (vrr_state_initialized_) vrr_state_.mark_restored();
  display_taken_ = false;
  return true;
}

bool DrmPresenter::release_scanout_resources(std::string& error) {
  if (scanout_released_) {
    error.clear();
    return scanout_cleanup_success_;
  }
  mode_blob_.reset();
  scanout_cleanup_success_ = buffers_.release(error);
  scanout_released_ = true;
  return scanout_cleanup_success_;
}

output::BackendStateResult DrmPresenter::shutdown(std::string& error) noexcept {
  if (shutdown_) {
    error = shutdown_error_;
    return shutdown_result_;
  }
  shutdown_ = true;
  clear_pending();
  bool kms_restore = !display_taken_;
  bool vt_restore = !direct_session_;
  bool master_drop = !device_.may_manage_master() || !master_owned_;
  bool framebuffer_cleanup = scanout_released_ && scanout_cleanup_success_;
  bool getty_restore = !direct_session_;
  try {
    std::string operation_error;
    if (direct_session_) {
      vt_restore = direct_session_->restore(operation_error);
      getty_restore = direct_session_->previous_terminal_restored();
      kms_restore = !display_taken_;
      master_drop = !master_owned_;
      direct_session_.reset();
    } else {
      kms_restore = restore_original_display(operation_error);
      if (kms_restore) {
        std::string cleanup_error;
        framebuffer_cleanup = release_scanout_resources(cleanup_error);
        if (!cleanup_error.empty()) {
          if (!operation_error.empty()) operation_error += "; ";
          operation_error += cleanup_error;
        }
      }
    }
    framebuffer_cleanup = scanout_released_ && scanout_cleanup_success_;
    const RestoreReport restore{kms_restore, vt_restore, master_drop,
                                framebuffer_cleanup};
    std::string report_error;
    bool vrr_report_ok = true;
    if (vrr_report_ && vrr_state_initialized_) {
      const auto timing = vrr_state_.timing_summary();
      const DrmVrrSummaryReport summary{
          timing.count,
          timing.within_threshold_count,
          timing.pass_basis_points,
          timing.minimum_nanoseconds,
          timing.maximum_nanoseconds,
          timing.mean_nanoseconds,
          timing.median_nanoseconds,
          timing.p95_absolute_error_nanoseconds,
          vrr_state_.enabled_period_count(),
          vrr_state_.disabled_period_count()};
      vrr_report_ok =
          append_vrr_report(DrmVrrReportRecord{summary}, report_error);
      if (vrr_report_ok) {
        const auto& state = vrr_state_.kms_state();
        const DrmVrrRestoreReport vrr_restore{
            state.original_enabled,
            vrr_state_.effective_enabled(),
            kms_restore &&
                (!state.crtc_property_present ||
                 vrr_state_.effective_enabled() == state.original_enabled),
            kms_restore,
            vt_restore,
            getty_restore};
        vrr_report_ok = append_vrr_report(DrmVrrReportRecord{vrr_restore},
                                          report_error);
      }
    }
    std::string standard_report_error;
    const bool report_ok = append_report(restore, standard_report_error);
    if (!operation_error.empty()) shutdown_error_ = operation_error;
    if (!vrr_report_ok) {
      if (!shutdown_error_.empty()) shutdown_error_ += "; ";
      shutdown_error_ += report_error;
    }
    if (!report_ok) {
      if (!shutdown_error_.empty()) shutdown_error_ += "; ";
      shutdown_error_ += standard_report_error;
    }
    if (!kms_restore || !vt_restore ||
        (device_.session() == DeviceSession::Standalone && !master_drop) ||
        !framebuffer_cleanup || !vrr_report_ok || !report_ok)
      shutdown_result_ = output::BackendStateResult::Fatal;
  } catch (...) {
    shutdown_error_ = "unexpected exception during DRM shutdown";
    shutdown_result_ = output::BackendStateResult::Fatal;
  }
  if (!scanout_released_) {
    mode_blob_.abandon();
    buffers_.abandon();
  }
  device_.reset();
  initialized_ = false;
  error = shutdown_error_;
  return shutdown_result_;
}

}  // namespace glasswyrm::drm
