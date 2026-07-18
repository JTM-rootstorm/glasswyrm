#include "backends/headless/presenter.hpp"

#include <cstdint>
#include <limits>
#include <vector>

namespace glasswyrm::headless {

output::PresentResult Presenter::present(
    const output::SoftwareFrameView& frame) {
  output::PresentResult present;
  if (frame.damage.size() > std::numeric_limits<std::uint32_t>::max()) {
    present.error = "frame damage count exceeds the headless manifest limit";
    return present;
  }

  const FrameDumpMetadata metadata{
      frame.ordinal,
      frame.commit_id,
      frame.generation,
      frame.output.output_id,
      frame.output.width,
      frame.output.height,
      static_cast<std::uint32_t>(frame.damage.size())};
  StagedFrameDump staged;
  if (!dumper_.stage(metadata, frame.pixels, staged, present.error)) {
    return present;
  }

  const auto canonical_hash = output::hash_visible_xrgb8888(frame.pixels);
  if (staged.fnv1a64() != canonical_hash) {
    dumper_.abort(staged);
    present.disposition = output::PresentDisposition::Fatal;
    present.error = "headless dump hash differs from the canonical software frame";
    return present;
  }

  FrameDumpResult dump;
  if (!dumper_.commit(staged, dump, present.error)) {
    dumper_.abort(staged);
    return present;
  }

  present.disposition = output::PresentDisposition::Complete;
  present.visible_hash = canonical_hash;
  return present;
}

output::PresentResult Presenter::present(
    const output::SoftwareFrameSetView &frames) {
  return present_frame_set(frames, true);
}

output::PresentResult Presenter::present_frame_set(
    const output::SoftwareFrameSetView &frames,
    const bool record_frame_set) {
  output::PresentResult present;
  if (!frames.valid() || frames.outputs->empty() ||
      frames.outputs->size() > output::SoftwareFrameSet::kMaximumOutputs) {
    present.error = "headless presenter requires one through eight output frames";
    return present;
  }
  std::vector<StagedFrameDump> staged(frames.outputs->size());
  std::size_t index = 0;
  for (const auto &[output_id, output_frame] : *frames.outputs) {
    if (output_id != output_frame.output.output_id ||
        output_frame.visible_hash != output_frame.frame.visible_hash() ||
        output_frame.damage.size() > std::numeric_limits<std::uint32_t>::max()) {
      present.error = "headless frame-set output metadata is inconsistent";
      return present;
    }
    const FrameDumpMetadata metadata{
        frames.ordinal,
        frames.commit_id,
        frames.generation,
        output_id,
        output_frame.output.width,
        output_frame.output.height,
        static_cast<std::uint32_t>(output_frame.damage.size())};
    if (!dumper_.stage(metadata, output_frame.frame.pixels(), staged[index],
                       present.error) ||
        staged[index].fnv1a64() != output_frame.visible_hash)
      return present;
    ++index;
  }
  std::vector<FrameDumpResult> committed;
  if (!dumper_.commit_all(staged, frames, committed, present.error,
                          record_frame_set)) {
    for (auto &frame : staged)
      dumper_.abort(frame);
    return present;
  }
  present.disposition = output::PresentDisposition::Complete;
  present.visible_hash = frames.aggregate_hash;
  return present;
}

output::BackendEvent Presenter::service(const short revents) {
  if (revents == 0) return {};
  return {output::BackendEventKind::Fatal, 0, 0,
          "headless presenter has no poll events"};
}

output::BackendStateResult Presenter::suspend(std::string& error) {
  error.clear();
  return output::BackendStateResult::Complete;
}

output::PresentResult Presenter::resume(
    const output::SoftwareFrameView& committed) {
  return present(committed);
}

output::PresentResult Presenter::resume(
    const output::SoftwareFrameSetView &committed) {
  return present_frame_set(committed, false);
}

}  // namespace glasswyrm::headless
