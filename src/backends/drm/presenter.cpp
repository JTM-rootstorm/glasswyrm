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
  bool promote_back{true};
  bool report_damage_copy{true};
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

void DrmPresenter::recover_vrr_divergence(std::string& error) noexcept {
  const auto primary = error;
  try {
    std::string recovery_error;
    const auto framebuffer =
        pending_ ? pending_->framebuffer_id : buffers_.front().framebuffer_id();
    const bool disabled = set_vrr_off_on_frame(framebuffer, recovery_error);
    std::string restore_error;
    const bool restored =
        restore_saved_state(kms_, device_.borrowed_kms_fd(), saved_,
                            restore_error);
    if (restored) {
      vrr_state_.mark_restored();
      display_taken_ = false;
    }
    error = primary;
    if (!disabled) {
      error += "; best-effort VRR-off failed";
      if (!recovery_error.empty()) error += ": " + recovery_error;
    }
    if (!restored) {
      error += "; saved KMS restore failed";
      if (!restore_error.empty()) error += ": " + restore_error;
    }
  } catch (...) {
    error = primary + "; unexpected exception during VRR recovery";
  }
}

output::PresentResult DrmPresenter::present(
    const output::SoftwareFrameView& frame) {
  return present_validated(frame, FullCopyReason::None, 0, nullptr, false);
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
  if (vrr_contract_enabled_ && vrr_state_initialized_ &&
      vrr_state_.kms_state().controllable) {
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
  if (vrr_contract_enabled_ && vrr_state_initialized_)
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
  if (vrr_request && vrr_request->valid && vrr_plan.desired_enabled)
    return present_initial_vrr_followup(frame, hash, *vrr_request, vrr_plan);
  output::PresentResult result{output::PresentDisposition::Complete, 0, hash,
                               {}};
  if (vrr_request && vrr_request->valid && vrr_contract_enabled_ &&
      vrr_state_initialized_)
    result.vrr_feedback.emplace(frame.output.output_id, vrr_state_.feedback());
  return result;
}

output::PresentResult DrmPresenter::present_initial_vrr_followup(
    const output::SoftwareFrameView& frame, const std::uint64_t hash,
    const output::VrrPresentationRequest& vrr_request,
    const PresenterVrrPlan& vrr_plan) {
  PendingPresentation value;
  value.token = next_token_++;
  value.hash = hash;
  value.ordinal = frame.ordinal;
  value.commit_id = frame.commit_id;
  value.generation = frame.generation;
  value.framebuffer_id = buffers_.front().framebuffer_id();
  value.next_front_index = front_index_;
  value.pixels.assign(frame.pixels.begin(), frame.pixels.end());
  value.cookie = std::make_shared<PageFlipCookie>(value.token);
  value.vrr_request = vrr_request;
  value.vrr_plan = vrr_plan;
  value.promote_back = false;
  value.report_damage_copy = false;
  std::string error;
  if (!device_.arm_page_flip(value.cookie, error)) {
    record_fatal("initial-vrr-followup", error);
    fatal_ = true;
    return {output::PresentDisposition::Fatal, 0, 0, error};
  }
  auto request = atomic_flip_request(
      pipeline_, saved_.properties, value.framebuffer_id);
  request = make_vrr_atomic_request(request, vrr_state_.kms_state(), true);
  if (!kms_.atomic_commit(device_.borrowed_kms_fd(), request,
                          AtomicNonblock | AtomicPageFlipEvent,
                          value.cookie.get(), error)) {
    device_.cancel_page_flip(value.cookie);
    record_fatal("initial-vrr-followup", error);
    fatal_ = true;
    return {output::PresentDisposition::Fatal, 0, 0, error};
  }
  pending_ = std::make_unique<PendingPresentation>(std::move(value));
  return {output::PresentDisposition::Pending, pending_->token, 0, {}};
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
    if (vrr_contract_enabled_ && vrr_state_initialized_ &&
        vrr_plan.include_property)
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
  if (vrr_contract_enabled_ && vrr_state_initialized_ &&
      vrr_state_.kms_state().controllable) {
    if (!read_kms_vrr_enabled(kms_, device_.borrowed_kms_fd(), pipeline_,
                              vrr_state_.kms_state(), readback_enabled,
                              error) ||
        readback_enabled != pending_->vrr_plan.desired_enabled) {
      if (error.empty())
        error = "CRTC VRR_ENABLED readback does not match the submitted state";
      recover_vrr_divergence(error);
      return fatal_event("page-flip-vrr-readback", std::move(error));
    }
    readback_valid = true;
  }
  if (vrr_contract_enabled_ && vrr_state_initialized_) {
    if (vrr_state_.kms_state().controllable && pending_->vrr_request &&
        (event.sequence == 0 ||
         (device_.snapshot().timestamp_monotonic &&
          (!event.timestamp_available ||
           event.kernel_timestamp_nanoseconds == 0)))) {
      error = "DRM page-flip timing is unavailable or invalid";
      recover_vrr_divergence(error);
      return fatal_event("page-flip-vrr-timing", std::move(error));
    }
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
    if (config_.damage_aware_copy && pending_->report_damage_copy)
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
    const DrmReportRecord decision{DrmVrrReportRecord{vrr_decision_report(
        *pending_->vrr_request, pending_->commit_id,
        pending_->generation, feedback.effective_enabled)}};
    if (feedback.flip_sequence == 0) {
      if (!vrr_report_->stage(decision, pending_->vrr_report, error))
        return fatal_event("page-flip-vrr-report", std::move(error));
    } else {
      const std::array<DrmReportRecord, 2> records{
          decision,
          DrmVrrReportRecord{vrr_timing_report(
              pending_->commit_id, pending_->generation,
              *pending_->vrr_request, feedback)}};
      if (!vrr_report_->stage(records, pending_->vrr_report, error))
        return fatal_event("page-flip-vrr-report", std::move(error));
    }
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
  if (pending_->promote_back) {
    complete_damage_copy(buffers_.back(), pending_->damage_copy,
                         pending_->generation);
    buffers_.promote_back();
    front_index_ = pending_->next_front_index;
  }
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

}  // namespace glasswyrm::drm
