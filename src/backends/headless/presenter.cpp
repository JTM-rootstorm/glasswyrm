#include "backends/headless/presenter.hpp"

#include <cstdint>
#include <limits>
#include <vector>

namespace glasswyrm::headless {

std::optional<output::VrrPresentationCapability>
Presenter::vrr_capability(const std::uint64_t output_id) const noexcept {
  if (!vrr_simulation_)
    return std::nullopt;
  const auto capability =
      vrr_simulation_->capability(output::OutputId{output_id});
  if (!capability)
    return std::nullopt;
  output::VrrPresentationCapability result;
  result.output_enabled = true;
  result.connected = true;
  result.connector_property_present = capability->connector_property_present;
  result.hardware_capable = capability->hardware_capable;
  result.atomic_test_passed = true;
  result.kms_controllable = capability->kms_controllable;
  result.simulated = capability->simulated;
  result.range_available = capability->range_available;
  result.atomic_required = capability->atomic_required;
  result.timing_available = true;
  result.minimum_refresh_millihertz =
      capability->minimum_refresh_millihertz;
  result.maximum_refresh_millihertz =
      capability->maximum_refresh_millihertz;
  result.reason_flags =
      output::vrr::reason_bit(output::vrr::Reason::SimulatedHeadless);
  return result;
}

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
  auto proposed_vrr = vrr_simulation_;
  output::VrrPresentationFeedbackMap vrr_feedback;
  std::size_t index = 0;
  for (const auto &[output_id, output_frame] : *frames.outputs) {
    if (output_id != output_frame.output.output_id ||
        output_frame.visible_hash != output_frame.frame.visible_hash() ||
        output_frame.damage.size() > std::numeric_limits<std::uint32_t>::max()) {
      present.error = "headless frame-set output metadata is inconsistent";
      return present;
    }
    const output::OutputId stable_output_id{output_id};
    if (proposed_vrr && proposed_vrr->configured(stable_output_id)) {
      const auto capability = proposed_vrr->capability(stable_output_id);
      if (!capability) {
        present.error = "headless VRR capability disappeared during presentation";
        return present;
      }
      const auto desired = output_frame.vrr.valid &&
                           output_frame.vrr.desired_enabled;
      const auto target_interval =
          output_frame.vrr.target_interval_nanoseconds != 0
              ? output_frame.vrr.target_interval_nanoseconds
              : refresh_interval_nanoseconds(
                    capability->minimum_refresh_millihertz);
      const auto simulated = proposed_vrr->present(
          stable_output_id, desired, target_interval, frames.commit_id,
          frames.generation, present.error);
      if (!simulated)
        return present;
      vrr_feedback.emplace(
          output_id,
          output::VrrPresentationFeedback{
              output_id,
              simulated->effective_enabled,
              true,
              true,
              simulated->flip_sequence,
              output::kVrrPresentationFeedbackSimulated,
              simulated->kernel_timestamp_nanoseconds,
              simulated->interval_nanoseconds,
              simulated->timestamp_available});
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
  if (vrr_report_ &&
      !vrr_report_->record_presentation(frames, vrr_feedback, present.error)) {
    present.disposition = output::PresentDisposition::Fatal;
    return present;
  }
  present.disposition = output::PresentDisposition::Complete;
  present.visible_hash = frames.aggregate_hash;
  present.vrr_feedback = std::move(vrr_feedback);
  vrr_simulation_ = std::move(proposed_vrr);
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

output::BackendStateResult Presenter::shutdown(std::string &error) noexcept {
  if (vrr_report_ && !vrr_report_->finish(error))
    return output::BackendStateResult::Fatal;
  error.clear();
  return output::BackendStateResult::Complete;
}

}  // namespace glasswyrm::headless
