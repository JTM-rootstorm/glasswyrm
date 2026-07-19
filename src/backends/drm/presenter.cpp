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
  std::shared_ptr<PageFlipCookie> cookie;
  headless::StagedFrameDump mirror;
  StagedDrmReport report;
  StagedDrmReport vrr_report;
  std::vector<std::uint32_t> pixels;
  DamageCopyPlan damage_copy;
  std::optional<output::VrrPresentationRequest> vrr_request;
  PresenterVrrPlan vrr_plan;
  std::optional<PresenterVrrState> completed_vrr_state;
  output::VrrPresentationFeedbackMap vrr_feedback;
  bool completion_verified{};
};
DrmPresenter::DrmPresenter(Device device, KmsApi& kms, DrmReport* report,
                           headless::FrameDumper* mirror,
                           DrmReport* vrr_report) noexcept
    : device_(std::move(device)),
      kms_(kms),
      report_(report),
      mirror_(mirror),
      vrr_report_(vrr_report),
      dumb_api_(kms, device_.borrowed_kms_fd()) {}
DrmPresenter::~DrmPresenter() { std::string ignored; (void)shutdown(ignored); }

std::optional<output::VrrPresentationCapability>
DrmPresenter::vrr_capability(const std::uint64_t output_id) const noexcept {
  if (!vrr_state_initialized_ || !initialized_ || output_id == 0 ||
      (config_.output.output_id != 0 && output_id != config_.output.output_id))
    return std::nullopt;
  const auto connector = std::ranges::find_if(
      device_.snapshot().connectors,
      [&](const Connector &value) { return value.id == pipeline_.connector; });
  const bool connected =
      connector != device_.snapshot().connectors.end() &&
      connector->status == ConnectionStatus::Connected;
  return vrr_state_.capability(true, connected);
}
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
  if (config.damage_aware_copy)
    damage_history_ = std::make_unique<DamageCopyHistory>(
        config.output.width, config.output.height);
  if (report_ && !report_->initialize(error)) return false;
  if (vrr_report_ && !vrr_report_->initialize(error)) return false;
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
                                   StagedDrmReport& vrr_report,
                                   std::string& error) {
  if (report.active() && !report_->commit(report, error)) return false;
  if (vrr_report.active() &&
      !vrr_report_->commit(vrr_report, error))
    return false;
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
        config_.output.width, config_.output.height,
        vrr_state_initialized_ && vrr_state_.kms_state().controllable);
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
  if (!vrr_state_initialized_ || !vrr_state_.kms_state().controllable) {
    error.clear();
    return true;
  }
  return verify_kms_vrr_enabled(kms_, device_.borrowed_kms_fd(), pipeline_,
                                vrr_state_.kms_state(), expected, error);
}

bool DrmPresenter::set_vrr_off_on_current_frame(std::string& error) {
  if (!vrr_state_initialized_ || !vrr_state_.kms_state().controllable ||
      !vrr_state_.effective_enabled()) {
    error.clear();
    return true;
  }
  auto request = atomic_flip_request(pipeline_, saved_.properties,
                                     buffers_.front().framebuffer_id());
  request = make_vrr_atomic_request(request, vrr_state_.kms_state(), false);
  if (!kms_.atomic_commit(device_.borrowed_kms_fd(), request, 0, nullptr,
                          error))
    return false;
  return verify_vrr_state(false, error);
}

output::PresentResult DrmPresenter::present(
    const output::SoftwareFrameView& frame) {
  return present_validated(frame, FullCopyReason::None, 0);
}

output::PresentResult DrmPresenter::present_initial(
    const output::SoftwareFrameView& frame, const std::uint64_t hash,
    const FullCopyReason forced_reason,
    const std::uint64_t layout_generation,
    const output::VrrPresentationRequest* const vrr_request,
    const PresenterVrrPlan& vrr_plan) {
  std::string error;
  auto& target = buffers_.front();
  headless::StagedFrameDump mirror;
  StagedDrmReport report;
  StagedDrmReport vrr_report;
  DamageCopyPlan damage_copy;
  if (!copy_frame_to(target, frame, hash, forced_reason, damage_copy, error) ||
      target.visible_hash() != hash) {
    error = error.empty() ? "canonical and scanout hashes differ" : error;
    record_fatal("initial-copy", error); fatal_ = true;
    return {output::PresentDisposition::Fatal, 0, 0, error};
  }
  const ModesetReport record{frame.ordinal, frame.commit_id, frame.generation, 0,
                             target.framebuffer_id(), hash, hash, selected_api_};
  bool report_staged = true;
  if (report_ && config_.damage_aware_copy) {
    const std::array<DrmReportRecord, 2> records{
        record, damage_copy_report(target, damage_copy, frame.generation, 0)};
    report_staged = report_->stage(records, report, error);
  } else if (report_) {
    report_staged = report_->stage(record, report, error);
  }
  if (!stage_mirror(frame, mirror, error) || !report_staged) {
    if (mirror_) mirror_->abort(mirror);
    if (report_) report_->abort(report);
    if (vrr_report_) vrr_report_->abort(vrr_report);
    return {output::PresentDisposition::Rejected, 0, 0, error};
  }
  if (!blocking_modeset(target, error)) {
    if (mirror_) mirror_->abort(mirror);
    if (report_) report_->abort(report);
    record_fatal("initial-modeset", error);
    fatal_ = true;
    return {output::PresentDisposition::Fatal, 0, 0, error};
  }
  bool readback_valid{};
  if (vrr_state_initialized_ && vrr_state_.kms_state().controllable) {
    if (!verify_vrr_state(false, error)) {
      if (mirror_) mirror_->abort(mirror);
      if (report_) report_->abort(report);
      if (vrr_report_) vrr_report_->abort(vrr_report);
      record_fatal("initial-vrr-readback", error);
      fatal_ = true;
      return {output::PresentDisposition::Fatal, 0, 0, error};
    }
    readback_valid = true;
  }
  if (vrr_state_initialized_)
    vrr_state_.complete_initial(false, readback_valid);
  if (vrr_report_ && vrr_request && vrr_request->valid) {
    const DrmReportRecord decision{DrmVrrReportRecord{vrr_decision_report(
        *vrr_request, frame.commit_id, frame.generation, false)}};
    if (!vrr_report_->stage(decision, vrr_report, error)) {
      if (mirror_) mirror_->abort(mirror);
      if (report_) report_->abort(report);
      record_fatal("initial-vrr-report", error);
      fatal_ = true;
      return {output::PresentDisposition::Fatal, 0, 0, error};
    }
  }
  display_taken_ = true;
  if (!commit_evidence(mirror, report, vrr_report, error)) {
    record_fatal("initial-evidence", error);
    fatal_ = true;
    return {output::PresentDisposition::Fatal, 0, 0, error};
  }
  committed_pixels_.assign(frame.pixels.begin(), frame.pixels.end());
  committed_hash_ = hash;
  committed_generation_ = frame.generation;
  if (layout_generation != 0)
    committed_layout_generation_ = layout_generation;
  complete_damage_copy(target, damage_copy, frame.generation);
  initial_modeset_ = true;
  output::PresentResult result{output::PresentDisposition::Complete, 0, hash,
                               {}};
  if (vrr_request && vrr_request->valid && vrr_state_initialized_)
    result.vrr_feedback.emplace(frame.output.output_id, vrr_state_.feedback());
  static_cast<void>(vrr_plan);
  return result;
}

output::PresentResult DrmPresenter::present_flip(
    const output::SoftwareFrameView& frame, const std::uint64_t hash,
    const FullCopyReason forced_reason,
    const std::uint64_t layout_generation,
    const output::VrrPresentationRequest* const vrr_request,
    const PresenterVrrPlan& vrr_plan) {
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
  value.cookie = std::make_shared<PageFlipCookie>(value.token);
  if (vrr_request && vrr_request->valid)
    value.vrr_request = *vrr_request;
  value.vrr_plan = vrr_plan;
  if (!copy_frame_to(target, frame, hash, forced_reason,
                     value.damage_copy, error) ||
      target.visible_hash() != hash) {
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
  bool report_staged = true;
  if (report_ && config_.damage_aware_copy) {
    const std::array<DrmReportRecord, 2> records{
        staged_record,
        damage_copy_report(target, value.damage_copy, frame.generation,
                           static_cast<std::uint32_t>(value.next_front_index))};
    report_staged = report_->stage(records, value.report, error);
  } else if (report_) {
    report_staged = report_->stage(staged_record, value.report, error);
  }
  if (!stage_mirror(frame, value.mirror, error) || !report_staged ||
      !device_.arm_page_flip(value.cookie, error)) {
    if (mirror_) mirror_->abort(value.mirror);
    if (report_) report_->abort(value.report);
    return {output::PresentDisposition::Rejected, 0, 0, error};
  }
  bool submitted{};
  if (selected_api_ == ReportApiPath::Atomic) {
    auto request = atomic_flip_request(
        pipeline_, saved_.properties, target.framebuffer_id());
    if (vrr_state_initialized_ && vrr_plan.include_property)
      request = make_vrr_atomic_request(request, vrr_state_.kms_state(),
                                        vrr_plan.desired_enabled);
    submitted = kms_.atomic_commit(device_.borrowed_kms_fd(), request,
                                   AtomicNonblock | AtomicPageFlipEvent,
                                   value.cookie.get(), error);
  } else {
    submitted = kms_.legacy_page_flip(device_.borrowed_kms_fd(), pipeline_.crtc,
                                      target.framebuffer_id(), *value.cookie,
                                      error);
  }
  if (!submitted) {
    device_.cancel_page_flip(value.cookie);
    if (mirror_) mirror_->abort(value.mirror);
    if (report_) report_->abort(value.report);
    return {output::PresentDisposition::Rejected, 0, 0, error};
  }
  pending_ = std::make_unique<PendingPresentation>(std::move(value));
  if (layout_generation != 0)
    pending_layout_generation_ = layout_generation;
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
      pending_->cookie->completed_crtc_id != pipeline_.crtc) {
    return fatal_event("page-flip-event",
                       "DRM page-flip completion did not match the pending frame");
  }
  const FlipReport record{pending_->ordinal, pending_->commit_id,
                          pending_->generation,
                          static_cast<std::uint32_t>(pending_->next_front_index),
                          pending_->framebuffer_id, pending_->hash,
                          pending_->hash, event.sequence, selected_api_};
  std::string error;
  bool readback_enabled{};
  bool readback_valid{};
  if (vrr_state_initialized_ && vrr_state_.kms_state().controllable) {
    if (!read_kms_vrr_enabled(kms_, device_.borrowed_kms_fd(), pipeline_,
                              vrr_state_.kms_state(), readback_enabled,
                              error) ||
        readback_enabled != pending_->vrr_plan.desired_enabled) {
      if (error.empty())
        error = "CRTC VRR_ENABLED readback does not match the submitted state";
      return fatal_event("page-flip-vrr-readback", std::move(error));
    }
    readback_valid = true;
  }
  if (vrr_state_initialized_) {
    auto completed = vrr_state_;
    completed.complete_flip(
        pending_->vrr_plan.desired_enabled, readback_enabled, readback_valid,
        event.sequence, event.kernel_timestamp_nanoseconds,
        event.timestamp_available);
    if (pending_->vrr_request) {
      const auto feedback = completed.feedback();
      pending_->vrr_feedback.emplace(feedback.output_id, feedback);
    }
    pending_->completed_vrr_state = std::move(completed);
  }
  if (report_) {
    report_->abort(pending_->report);
    std::vector<DrmReportRecord> records{record};
    if (config_.damage_aware_copy)
      records.emplace_back(damage_copy_report(
          buffers_.back(), pending_->damage_copy, pending_->generation,
          static_cast<std::uint32_t>(pending_->next_front_index)));
    if (!report_->stage(records, pending_->report, error)) {
      return fatal_event("page-flip-report", std::move(error));
    }
  }
  if (vrr_report_ && pending_->vrr_request &&
      pending_->completed_vrr_state) {
    vrr_report_->abort(pending_->vrr_report);
    const auto feedback = pending_->completed_vrr_state->feedback();
    const std::array<DrmReportRecord, 2> records{
        DrmVrrReportRecord{vrr_decision_report(
            *pending_->vrr_request, pending_->commit_id,
            pending_->generation, feedback.effective_enabled)},
        DrmVrrReportRecord{vrr_timing_report(
            pending_->commit_id, pending_->generation,
            *pending_->vrr_request, feedback)}};
    if (!vrr_report_->stage(records, pending_->vrr_report, error))
      return fatal_event("page-flip-vrr-report", std::move(error));
  }
  pending_->completion_verified = true;
  return {output::BackendEventKind::Complete,
          pending_->token,
          pending_frame_set_hash_.value_or(pending_->hash),
          {},
          pending_->vrr_feedback};
}

bool DrmPresenter::finalize_pending(const std::uint64_t token,
                                    std::string& error) {
  error.clear();
  if (!pending_ || pending_->token != token ||
      !pending_->completion_verified) {
    error = "DRM pending completion token is not verified";
    return false;
  }
  if (!commit_evidence(pending_->mirror, pending_->report,
                       pending_->vrr_report, error)) {
    record_fatal("page-flip-evidence", error);
    fatal_ = true;
    clear_pending();
    return false;
  }
  complete_damage_copy(buffers_.back(), pending_->damage_copy,
                       pending_->generation);
  buffers_.promote_back();
  front_index_ = pending_->next_front_index;
  committed_pixels_ = std::move(pending_->pixels);
  committed_hash_ = pending_->hash;
  committed_generation_ = pending_->generation;
  if (pending_layout_generation_)
    committed_layout_generation_ = *pending_layout_generation_;
  if (pending_->completed_vrr_state)
    vrr_state_ = *pending_->completed_vrr_state;
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
  if (pending_->cookie) device_.abandon_page_flip(pending_->cookie);
  if (mirror_) mirror_->abort(pending_->mirror);
  if (report_) report_->abort(pending_->report);
  if (vrr_report_) vrr_report_->abort(pending_->vrr_report);
  pending_.reset();
  pending_frame_set_hash_.reset();
  pending_layout_generation_.reset();
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
  if (vrr_state_initialized_)
    vrr_state_.mark_suspended_off();
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
  if (vrr_state_initialized_)
    vrr_state_.mark_session_active();
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
  if (!copy_frame_to(target, frame, committed_hash_, FullCopyReason::VirtualTerminalResume,
                     damage_copy, error) ||
      target.visible_hash() != committed_hash_) {
    if (error.empty()) error = "re-modeset scanout hash differs from committed frame";
    return false;
  }
  const auto next_front_index = static_cast<std::uint32_t>(1U - front_index_);
  const auto damage_report = damage_copy_report(
      target, damage_copy, committed_generation_, next_front_index);
  if (!blocking_modeset(target, error)) return false;
  if (!verify_vrr_state(false, error)) return false;
  if (vrr_state_initialized_)
    vrr_state_.mark_acquired_off();
  complete_damage_copy(target, damage_copy, committed_generation_);
  buffers_.promote_back();
  front_index_ = 1U - front_index_;
  display_taken_ = true;
  if (direct_session_) {
    if (config_.damage_aware_copy &&
        !append_report(damage_report, error))
      return false;
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
  if (vrr_state_initialized_)
    vrr_state_.mark_restored();
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
            false};
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
