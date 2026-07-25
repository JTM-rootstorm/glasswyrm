#include "backends/drm/presenter.hpp"

#include <limits>

namespace glasswyrm::drm {

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

bool DrmPresenter::commit_evidence(
    headless::StagedFrameDump& mirror, StagedDrmReport& report,
    StagedDrmReport& vrr_report,
    const DrmPresentationEvidenceId& evidence, std::string& error) {
  const bool sealed = evidence.presentation_token != 0;
  if (sealed &&
      (!report_ || !vrr_report_ || !report.active() ||
       !vrr_report.active())) {
    error = "sealed DRM evidence is missing a required staged stream";
    return false;
  }

  const bool mirror_required = mirror.active();
  const auto mirror_frame = mirror.metadata().frame;
  if (report.active() && !report_->commit(report, error)) return false;
  if (vrr_report.active() && !vrr_report_->commit(vrr_report, error))
    return false;

  headless::FrameDumpResult mirror_result;
  if (mirror.active() && !mirror_->commit(mirror, mirror_result, error))
    return false;
  if (!sealed) return true;

  std::uint32_t streams =
      kEvidenceStreamDrmReport | kEvidenceStreamVrrReport;
  if (mirror_required) streams |= kEvidenceStreamMirror;
  const EvidenceSealReport seal{
      evidence,
      streams,
      streams,
      mirror_required ? mirror_frame : 0,
      mirror_required ? mirror_result.fnv1a64 : 0,
      mirror_required ? mirror_result.file.filename().string() : std::string{}};
  StagedDrmReport staged_seal;
  return vrr_report_->stage(DrmReportRecord{seal}, staged_seal, error) &&
         vrr_report_->commit(staged_seal, error);
}

}  // namespace glasswyrm::drm
