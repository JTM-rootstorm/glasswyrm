#include "backends/drm/presenter.hpp"

namespace glasswyrm::drm {

output::PresentResult
DrmPresenter::present(const output::SoftwareFrameSetView &frames) {
  if (!frames.valid() || frames.outputs->size() != 1)
    return {output::PresentDisposition::Rejected, 0, 0,
            "DRM presenter supports exactly one output frame"};
  const auto &output_frame = frames.outputs->begin()->second;
  if (output_frame.output.output_id != frames.outputs->begin()->first ||
      output_frame.visible_hash != output_frame.frame.visible_hash())
    return {output::PresentDisposition::Rejected, 0, 0,
            "DRM frame-set output metadata is inconsistent"};
  const output::SoftwareFrameView frame{
      output_frame.output, output_frame.frame.pixels(), output_frame.damage,
      frames.commit_id, frames.generation, frames.ordinal};
  auto result = present(frame);
  if (result.disposition == output::PresentDisposition::Complete)
    result.visible_hash = frames.aggregate_hash;
  else if (result.disposition == output::PresentDisposition::Pending)
    pending_frame_set_hash_ = frames.aggregate_hash;
  return result;
}

output::PresentResult
DrmPresenter::resume(const output::SoftwareFrameSetView &committed) {
  if (!committed.valid() || committed.outputs->size() != 1)
    return {output::PresentDisposition::Rejected, 0, 0,
            "DRM resume supports exactly one output frame"};
  const auto &output_frame = committed.outputs->begin()->second;
  const output::SoftwareFrameView frame{
      output_frame.output, output_frame.frame.pixels(), output_frame.damage,
      committed.commit_id, committed.generation, committed.ordinal};
  auto result = resume(frame);
  if (result.disposition == output::PresentDisposition::Complete)
    result.visible_hash = committed.aggregate_hash;
  return result;
}

} // namespace glasswyrm::drm
