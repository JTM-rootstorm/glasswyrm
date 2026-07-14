#include "backends/drm/presenter.hpp"

#include "backends/drm/connector_name.hpp"
#include "backends/drm/connector_selector.hpp"
#include "backends/drm/mode_selector.hpp"
#include "backends/drm/pipeline_selector.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <poll.h>
#include <utility>

namespace glasswyrm::drm {
struct DrmPresenter::PendingPresentation {
  std::uint64_t token{};
  std::uint64_t hash{};
  std::uint64_t ordinal{};
  std::uint64_t commit_id{};
  std::uint64_t generation{};
  std::uint32_t framebuffer_id{};
  std::size_t next_front_index{};
  std::unique_ptr<PageFlipCookie> cookie;
  headless::StagedFrameDump mirror;
  StagedDrmReport report;
  std::vector<std::uint32_t> pixels;
  bool completion_verified{};
};
DrmPresenter::DrmPresenter(Device device, KmsApi& kms, DrmReport* report,
                           headless::FrameDumper* mirror) noexcept
    : device_(std::move(device)),
      kms_(kms),
      report_(report),
      mirror_(mirror),
      dumb_api_(kms, device_.borrowed_kms_fd()) {}
DrmPresenter::~DrmPresenter() { std::string ignored; (void)shutdown(ignored); }
bool DrmPresenter::initialize(const DrmPresenterConfig& config,
                              session::VirtualTerminalApi* vt_api,
                              std::string& error) {
  error.clear();
  if (initialized_ || shutdown_ || !device_.valid()) {
    error = "DRM presenter cannot initialize in its current state";
    return false;
  }
  const auto pixels = std::uint64_t{config.output.width} * config.output.height;
  if (config.output.width == 0 || config.output.height == 0 ||
      pixels > output::SoftwareFrame::kMaximumPixels) {
    error = "DRM presenter output dimensions are invalid";
    return false;
  }
  config_ = config;
  if (report_ && !report_->initialize(error)) return false;
  if (!select_pipeline(error)) {
    record_fatal("initialization", error);
    std::string ignored; (void)shutdown(ignored);
    return false;
  }

  const bool direct = device_.session() == DeviceSession::Standalone;
  if (direct) {
    if (!vt_api || config.tty_path.empty()) {
      error = "direct DRM presentation requires a virtual terminal";
      record_fatal("session", error);
      std::string ignored; (void)shutdown(ignored);
      return false;
    }
    direct_session_ =
        std::make_unique<session::DirectVirtualTerminalSession>(*vt_api, *this);
    if (!direct_session_->acquire(config.tty_path, config.vt_signals, error)) {
      record_fatal("session", error);
      std::string ignored; (void)shutdown(ignored);
      return false;
    }
  } else if (vt_api || !config.tty_path.empty()) {
    error = "external DRM presentation forbids virtual-terminal ownership";
    record_fatal("session", error);
    std::string ignored; (void)shutdown(ignored);
    return false;
  } else {
    bool external_master = false;
    if (!kms_.is_master(device_.borrowed_kms_fd(), external_master, error) ||
        !external_master) {
      if (error.empty())
        error = "external DRM device FD is not current DRM master";
      record_fatal("session", error);
      std::string ignored;
      (void)shutdown(ignored);
      return false;
    }
  }

  if (!DumbBufferPair::create(dumb_api_, config.output.width,
                              config.output.height, buffers_, error) ||
      !configure_api(error) || !initialize_report(error)) {
    record_fatal("pipeline", error);
    std::string ignored; (void)shutdown(ignored);
    return false;
  }
  initialized_ = true;
  return true;
}
bool DrmPresenter::stage_mirror(const output::SoftwareFrameView& frame,
                                headless::StagedFrameDump& staged,
                                std::string& error) const {
  if (!mirror_) return true;
  if (frame.damage.size() > std::numeric_limits<std::uint32_t>::max()) {
    error = "DRM mirror damage count exceeds the manifest limit";
    return false;
  }
  return mirror_->stage({frame.ordinal, frame.commit_id, frame.generation,
                         frame.output.output_id, frame.output.width,
                         frame.output.height,
                         static_cast<std::uint32_t>(frame.damage.size())},
                        frame.pixels, staged, error);
}

bool DrmPresenter::commit_evidence(headless::StagedFrameDump& mirror,
                                   StagedDrmReport& report,
                                   std::string& error) {
  if (report.active() && !report_->commit(report, error)) return false;
  if (mirror.active()) {
    headless::FrameDumpResult result;
    if (!mirror_->commit(mirror, result, error)) return false;
  }
  return true;
}

bool DrmPresenter::blocking_modeset(DumbBuffer& buffer, std::string& error) {
  if (selected_api_ == ReportApiPath::Atomic) {
    const auto request = atomic_initial_request(
        pipeline_, saved_.properties, mode_blob_.id(), buffer.framebuffer_id(),
        config_.output.width, config_.output.height);
    return kms_.atomic_commit(device_.borrowed_kms_fd(), request,
                              AtomicAllowModeset, nullptr, error);
  }
  const KmsCrtcState state{pipeline_.crtc, buffer.framebuffer_id(), 0, 0, true,
                           kms_mode_};
  const std::array connector{pipeline_.connector};
  return kms_.legacy_set_crtc(device_.borrowed_kms_fd(), state, connector,
                              error);
}

output::PresentResult DrmPresenter::present(
    const output::SoftwareFrameView& frame) {
  if (!initialized_ || shutdown_ || fatal_)
    return {output::PresentDisposition::Fatal, 0, 0,
            "DRM presenter is not operational"};
  if (suspended_)
    return {output::PresentDisposition::Rejected, 0, 0,
            "DRM presenter is suspended"};
  if (pending_)
    return {output::PresentDisposition::Rejected, 0, 0,
            "one DRM page flip is already pending"};
  const auto expected = std::uint64_t{config_.output.width} * config_.output.height;
  const auto refresh_distance =
      frame.output.refresh_millihz > config_.output.refresh_millihz
          ? frame.output.refresh_millihz - config_.output.refresh_millihz
          : config_.output.refresh_millihz - frame.output.refresh_millihz;
  if (frame.output.width != config_.output.width ||
      frame.output.height != config_.output.height ||
      frame.output.output_id == 0 ||
      (config_.output.output_id != 0 &&
       frame.output.output_id != config_.output.output_id) ||
      refresh_distance > kDefaultRefreshToleranceMillihz ||
      frame.commit_id == 0 || frame.generation == 0 || frame.ordinal == 0 ||
      frame.pixels.size() != expected)
    return {output::PresentDisposition::Rejected, 0, 0,
            "software frame does not match the selected DRM mode"};
  const auto hash = output::hash_visible_xrgb8888(frame.pixels);
  return initial_modeset_ ? present_flip(frame, hash)
                          : present_initial(frame, hash);
}

output::PresentResult DrmPresenter::present_initial(
    const output::SoftwareFrameView& frame, const std::uint64_t hash) {
  std::string error;
  auto& target = buffers_.front();
  headless::StagedFrameDump mirror;
  StagedDrmReport report;
  if (!target.copy_from(frame.pixels, error) || target.visible_hash() != hash) {
    error = error.empty() ? "canonical and scanout hashes differ" : error;
    record_fatal("initial-copy", error); fatal_ = true;
    return {output::PresentDisposition::Fatal, 0, 0, error};
  }
  const ModesetReport record{frame.ordinal, frame.commit_id, frame.generation, 0,
                             target.framebuffer_id(), hash, hash, selected_api_};
  if (!stage_mirror(frame, mirror, error) ||
      (report_ && !report_->stage(record, report, error))) {
    if (mirror_) mirror_->abort(mirror);
    if (report_) report_->abort(report);
    return {output::PresentDisposition::Rejected, 0, 0, error};
  }
  if (!blocking_modeset(target, error)) {
    if (mirror_) mirror_->abort(mirror);
    if (report_) report_->abort(report);
    record_fatal("initial-modeset", error);
    fatal_ = true;
    return {output::PresentDisposition::Fatal, 0, 0, error};
  }
  display_taken_ = true;
  if (!commit_evidence(mirror, report, error)) {
    record_fatal("initial-evidence", error);
    fatal_ = true;
    return {output::PresentDisposition::Fatal, 0, 0, error};
  }
  committed_pixels_.assign(frame.pixels.begin(), frame.pixels.end());
  committed_hash_ = hash;
  initial_modeset_ = true;
  return {output::PresentDisposition::Complete, 0, hash, {}};
}

output::PresentResult DrmPresenter::present_flip(
    const output::SoftwareFrameView& frame, const std::uint64_t hash) {
  std::string error;
  auto& target = buffers_.back();
  PendingPresentation value;
  value.token = next_token_++;
  value.hash = hash;
  value.ordinal = frame.ordinal;
  value.commit_id = frame.commit_id;
  value.generation = frame.generation;
  value.framebuffer_id = target.framebuffer_id();
  value.next_front_index = 1U - front_index_;
  value.pixels.assign(frame.pixels.begin(), frame.pixels.end());
  value.cookie = std::make_unique<PageFlipCookie>(value.token);
  if (!target.copy_from(frame.pixels, error) || target.visible_hash() != hash) {
    error = error.empty() ? "canonical and scanout hashes differ" : error;
    record_fatal("flip-copy", error);
    fatal_ = true;
    return {output::PresentDisposition::Fatal, 0, 0, error};
  }
  const FlipReport staged_record{
      value.ordinal,
      value.commit_id,
      value.generation,
      static_cast<std::uint32_t>(value.next_front_index),
      value.framebuffer_id,
      value.hash,
      value.hash,
      std::numeric_limits<std::uint64_t>::max(),
      selected_api_};
  if (!stage_mirror(frame, value.mirror, error) ||
      (report_ && !report_->stage(staged_record, value.report, error)) ||
      !device_.arm_page_flip(*value.cookie, error)) {
    if (mirror_) mirror_->abort(value.mirror);
    if (report_) report_->abort(value.report);
    return {output::PresentDisposition::Rejected, 0, 0, error};
  }
  bool submitted{};
  if (selected_api_ == ReportApiPath::Atomic) {
    const auto request = atomic_flip_request(
        pipeline_, saved_.properties, target.framebuffer_id());
    submitted = kms_.atomic_commit(device_.borrowed_kms_fd(), request,
                                   AtomicNonblock | AtomicPageFlipEvent,
                                   value.cookie.get(), error);
  } else {
    submitted = kms_.legacy_page_flip(device_.borrowed_kms_fd(), pipeline_.crtc,
                                      target.framebuffer_id(), *value.cookie,
                                      error);
  }
  if (!submitted) {
    device_.disarm_page_flip(*value.cookie);
    if (mirror_) mirror_->abort(value.mirror);
    if (report_) report_->abort(value.report);
    return {output::PresentDisposition::Rejected, 0, 0, error};
  }
  pending_ = std::make_unique<PendingPresentation>(std::move(value));
  return {output::PresentDisposition::Pending, pending_->token, 0, {}};
}

int DrmPresenter::poll_fd() const noexcept { return device_.poll_fd(); }

short DrmPresenter::poll_events() const noexcept { return initialized_ ? POLLIN : 0; }

output::BackendEvent DrmPresenter::service(const short revents) {
  const auto event = device_.service_events(revents);
  if (event.kind == DrmEventKind::None) return {};
  if (event.kind == DrmEventKind::Error)
    return fatal_event("page-flip-event", event.error);
  if (!pending_ || !pending_->cookie || event.token != pending_->token ||
      event.crtc_id != pipeline_.crtc || !pending_->cookie->completed ||
      pending_->cookie->completed_crtc_id != pipeline_.crtc ||
      event.sequence == 0) {
    return fatal_event("page-flip-event",
                       "DRM page-flip completion did not match the pending frame");
  }
  const FlipReport record{pending_->ordinal, pending_->commit_id,
                          pending_->generation,
                          static_cast<std::uint32_t>(pending_->next_front_index),
                          pending_->framebuffer_id, pending_->hash,
                          pending_->hash, event.sequence, selected_api_};
  std::string error;
  if (report_) {
    report_->abort(pending_->report);
    if (!report_->stage(record, pending_->report, error))
      return fatal_event("page-flip-report", std::move(error));
  }
  pending_->completion_verified = true;
  return {output::BackendEventKind::Complete, pending_->token, pending_->hash,
          {}};
}

bool DrmPresenter::finalize_pending(const std::uint64_t token,
                                    std::string& error) {
  error.clear();
  if (!pending_ || pending_->token != token ||
      !pending_->completion_verified) {
    error = "DRM pending completion token is not verified";
    return false;
  }
  if (!commit_evidence(pending_->mirror, pending_->report, error)) {
    record_fatal("page-flip-evidence", error);
    fatal_ = true;
    clear_pending();
    return false;
  }
  buffers_.promote_back();
  front_index_ = pending_->next_front_index;
  committed_pixels_ = std::move(pending_->pixels);
  committed_hash_ = pending_->hash;
  clear_pending();
  return true;
}

void DrmPresenter::abort_pending(const std::uint64_t token,
                                 const std::string_view reason) noexcept {
  if (!pending_ || pending_->token != token) return;
  if (!reason.empty()) {
    record_fatal("pending-presentation-abort",
                 std::string(reason.substr(0, kMaximumDiagnosticBytes)));
    fatal_ = true;
  }
  clear_pending();
}

void DrmPresenter::clear_pending() noexcept {
  if (!pending_) return;
  if (pending_->cookie) device_.disarm_page_flip(*pending_->cookie);
  if (mirror_) mirror_->abort(pending_->mirror);
  if (report_) report_->abort(pending_->report);
  pending_.reset();
}

output::BackendEvent DrmPresenter::fatal_event(std::string stage,
                                               std::string reason) {
  record_fatal(stage, reason);
  clear_pending();
  fatal_ = true;
  return {output::BackendEventKind::Fatal, 0, 0, std::move(reason)};
}

void DrmPresenter::record_fatal(std::string stage, std::string reason) noexcept {
  try {
    const FatalReport record{std::move(stage), std::move(reason),
                             config_.connector.value_or("auto"), pipeline_.crtc,
                             pending_ ? pending_->framebuffer_id : 0,
                             pending_ ? pending_->commit_id : 0,
                             pending_ ? pending_->generation : 0};
    if (pending_ && report_)
      report_->abort(pending_->report);
    std::string ignored;
    (void)append_report(record, ignored);
  } catch (...) {
  }
}

bool DrmPresenter::append_report(const DrmReportRecord& record,
                                 std::string& error) {
  StagedDrmReport staged;
  if (!report_) { error.clear(); return true; }
  return report_->stage(record, staged, error) && report_->commit(staged, error);
}

output::BackendStateResult DrmPresenter::suspend(std::string& error) {
  if (pending_) {
    error = "cannot release the VT while a page flip remains pending";
    record_fatal("vt-release", error);
    fatal_ = true;
    return output::BackendStateResult::Fatal;
  }
  if (direct_session_ && !direct_session_->release(error)) {
    record_fatal("vt-release", error);
    fatal_ = true;
    return output::BackendStateResult::Fatal;
  }
  suspended_ = true;
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
  if (!target.copy_from(committed_pixels_, error) ||
      target.visible_hash() != committed_hash_) {
    if (error.empty()) error = "re-modeset scanout hash differs from committed frame";
    return false;
  }
  if (!blocking_modeset(target, error)) return false;
  buffers_.promote_back();
  front_index_ = 1U - front_index_;
  display_taken_ = true;
  if (direct_session_) {
    const VtReport record{VtTransition::Acquire, true, true, committed_hash_};
    return append_report(record, error);
  }
  return true;
}

bool DrmPresenter::restore_original_display(std::string& error) {
  if (!display_taken_) {
    error.clear();
    return true;
  }
  if (!restore_saved_state(kms_, device_.borrowed_kms_fd(), saved_, error))
    return false;
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
  if (shutdown_) { error = shutdown_error_; return shutdown_result_; }
  shutdown_ = true;
  clear_pending();
  bool kms_restore = !display_taken_;
  bool vt_restore = !direct_session_;
  bool master_drop = !device_.may_manage_master() || !master_owned_;
  bool framebuffer_cleanup = scanout_released_ && scanout_cleanup_success_;
  try {
    std::string operation_error;
    if (direct_session_) {
      vt_restore = direct_session_->restore(operation_error);
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
    const bool report_ok = append_report(restore, report_error);
    if (!operation_error.empty()) shutdown_error_ = operation_error;
    if (!report_ok) {
      if (!shutdown_error_.empty()) shutdown_error_ += "; ";
      shutdown_error_ += report_error;
    }
    if (!kms_restore || !vt_restore ||
        (device_.session() == DeviceSession::Standalone && !master_drop) ||
        !framebuffer_cleanup || !report_ok)
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
