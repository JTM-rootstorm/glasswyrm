#include "backends/drm/presenter.hpp"

#include "backends/drm/mode_selector.hpp"

namespace glasswyrm::drm {

output::PresentResult DrmPresenter::present_validated(
    const output::SoftwareFrameView &frame,
    const FullCopyReason forced_reason,
    const std::uint64_t layout_generation) {
  if (!initialized_ || shutdown_ || fatal_)
    return {output::PresentDisposition::Fatal, 0, 0,
            "DRM presenter is not operational"};
  if (suspended_)
    return {output::PresentDisposition::Rejected, 0, 0,
            "DRM presenter is suspended"};
  if (pending_)
    return {output::PresentDisposition::Rejected, 0, 0,
            "one DRM page flip is already pending"};
  const auto expected =
      std::uint64_t{config_.output.width} * config_.output.height;
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
  return initial_modeset_
             ? present_flip(frame, hash, forced_reason, layout_generation)
             : present_initial(frame, hash, forced_reason, layout_generation);
}

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
  const auto reason =
      committed_layout_generation_ != 0 &&
              committed_layout_generation_ != frames.layout_generation
          ? FullCopyReason::OutputConfigurationChanged
          : FullCopyReason::None;
  auto result = present_validated(frame, reason, frames.layout_generation);
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
