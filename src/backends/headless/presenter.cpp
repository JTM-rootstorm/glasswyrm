#include "backends/headless/presenter.hpp"

#include <cstdint>
#include <limits>

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

}  // namespace glasswyrm::headless
