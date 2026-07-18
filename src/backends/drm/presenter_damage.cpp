#include "backends/drm/presenter.hpp"

#include <limits>

namespace glasswyrm::drm {
namespace {

std::uint64_t saturating_add(const std::uint64_t left,
                             const std::uint64_t right) noexcept {
  const auto maximum = std::numeric_limits<std::uint64_t>::max();
  return right > maximum - left ? maximum : left + right;
}

}  // namespace

bool DrmPresenter::copy_frame_to(
    DumbBuffer& target, const output::SoftwareFrameView& frame,
    const std::uint64_t expected_hash, const FullCopyReason forced_reason,
    DamageCopyPlan& plan,
    std::string& error) {
  if (!config_.damage_aware_copy) return target.copy_from(frame.pixels, error);
  if (!damage_history_) {
    error = "damage-aware DRM copy history is unavailable";
    return false;
  }
  plan = damage_history_->plan(target.content_valid(),
                               target.completed_generation(),
                               frame.generation, frame.damage, forced_reason);
  const bool copied = plan.full_copy()
                          ? target.copy_from(frame.pixels, error)
                          : target.copy_rectangles_from(frame.pixels,
                                                        plan.rectangles, error);
  if (!copied || plan.full_copy() || target.visible_hash() == expected_hash)
    return copied;

  // Damage is an optimization hint, never a correctness boundary. If an
  // upstream producer changed pixels outside its advertised region, recover
  // before KMS submission and retain full-output history for this generation.
  plan = damage_history_->plan(target.content_valid(),
                               target.completed_generation(),
                               frame.generation, frame.damage,
                               FullCopyReason::CanonicalMismatch);
  return target.copy_from(frame.pixels, error);
}

DamageCopyReport DrmPresenter::damage_copy_report(
    const DumbBuffer& target, const DamageCopyPlan& plan,
    const std::uint64_t generation,
    const std::uint32_t buffer_index) const {
  DamageCopyReport report;
  report.generation = generation;
  report.buffer_index = buffer_index;
  report.framebuffer_id = target.framebuffer_id();
  report.full_frame_bytes = plan.full_frame_bytes;
  report.copied_bytes = plan.copied_bytes;
  report.history_span = plan.history_span;
  report.cumulative_full_frame_bytes = saturating_add(
      cumulative_full_frame_bytes_, plan.full_frame_bytes);
  report.cumulative_copied_bytes = saturating_add(
      cumulative_copied_bytes_, plan.copied_bytes);
  report.rectangles = plan.rectangles;
  report.full_copy_reason = plan.full_copy_reason;
  return report;
}

void DrmPresenter::complete_damage_copy(DumbBuffer& target,
                                        const DamageCopyPlan& plan,
                                        const std::uint64_t generation) {
  if (!config_.damage_aware_copy || !damage_history_) return;
  target.mark_completed(generation);
  damage_history_->record_completed(plan, generation);
  cumulative_full_frame_bytes_ = saturating_add(
      cumulative_full_frame_bytes_, plan.full_frame_bytes);
  cumulative_copied_bytes_ = saturating_add(
      cumulative_copied_bytes_, plan.copied_bytes);
}

}  // namespace glasswyrm::drm
