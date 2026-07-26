#include "backends/drm/presenter.hpp"
#include "backends/drm/presenter_pending.hpp"

#include <cstring>
#include <limits>
#include <new>

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
  if (!copied)
    return false;
  const auto first_copy = target.last_copy_metrics();
  plan.drm_copied_bytes = first_copy.bytes;
  plan.copy_nanoseconds = first_copy.nanoseconds;
  const bool parity =
      plan.full_copy()
          ? target.verify_visible_pixels(frame.pixels, expected_hash)
          : target.verify_damage_lineage(
                frame.pixels, plan.rectangles, expected_hash);
  if (parity) {
    const auto parity = target.last_parity_metrics();
    plan.parity_verified_bytes = plan.full_frame_bytes;
    plan.scanout_readback_bytes = parity.bytes;
    plan.parity_nanoseconds = parity.nanoseconds;
    return true;
  }
  const auto first_parity = target.last_parity_metrics();
  if (plan.full_copy()) {
    error = "canonical and scanout pixels differ after a complete copy";
    return false;
  }

  // Damage is an optimization hint, never a correctness boundary. If an
  // upstream producer changed pixels outside its advertised region, recover
  // before KMS submission and retain full-output history for this generation.
  plan = damage_history_->plan(target.content_valid(),
                               target.completed_generation(),
                               frame.generation, frame.damage,
                               FullCopyReason::CanonicalMismatch);
  if (!target.copy_from(frame.pixels, error))
    return false;
  const auto recovery_copy = target.last_copy_metrics();
  if (!target.verify_visible_pixels(frame.pixels, expected_hash)) {
    error = "canonical and scanout pixels differ after mismatch recovery";
    return false;
  }
  const auto recovery_parity = target.last_parity_metrics();
  plan.drm_copied_bytes =
      saturating_add(first_copy.bytes, recovery_copy.bytes);
  plan.copy_nanoseconds =
      saturating_add(first_copy.nanoseconds, recovery_copy.nanoseconds);
  plan.parity_verified_bytes =
      saturating_add(first_parity.bytes, recovery_parity.bytes);
  plan.scanout_readback_bytes =
      saturating_add(first_parity.bytes, recovery_parity.bytes);
  plan.parity_nanoseconds =
      saturating_add(first_parity.nanoseconds, recovery_parity.nanoseconds);
  return true;
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
  report.drm_copied_bytes = plan.drm_copied_bytes;
  report.copy_nanoseconds = plan.copy_nanoseconds;
  report.parity_verified_bytes = plan.parity_verified_bytes;
  report.scanout_readback_bytes = plan.scanout_readback_bytes;
  report.parity_nanoseconds = plan.parity_nanoseconds;
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

bool DrmPresenter::stage_committed_pixel_update(
    PendingPresentation& pending,
    const std::span<const std::uint32_t> pixels,
    std::string& error) const {
  const auto expected =
      std::uint64_t{config_.output.width} * config_.output.height;
  if (expected != pixels.size() || expected != committed_pixels_.size()) {
    error = "pending DRM frame cannot update committed pixel history";
    return false;
  }
  try {
    pending.committed_pixel_rectangles =
        config_.damage_aware_copy
            ? pending.damage_copy.rectangles
            : std::vector<gw::compositor::Rectangle>{
                  {0, 0, config_.output.width, config_.output.height}};
  } catch (const std::bad_alloc&) {
    error = "could not retain pending DRM pixel rectangles";
    return false;
  }
  if (pending.committed_pixel_rectangles.empty()) {
    error = "pending DRM frame has no committed pixel update";
    return false;
  }

  std::size_t update_pixels = 0;
  for (const auto& rectangle : pending.committed_pixel_rectangles) {
    const auto right = std::int64_t{rectangle.x} + rectangle.width;
    const auto bottom = std::int64_t{rectangle.y} + rectangle.height;
    const auto count = std::uint64_t{rectangle.width} * rectangle.height;
    if (rectangle.empty() || rectangle.x < 0 || rectangle.y < 0 ||
        right > config_.output.width || bottom > config_.output.height ||
        count > std::numeric_limits<std::size_t>::max() - update_pixels) {
      error = "pending DRM pixel update exceeds the selected output";
      return false;
    }
    update_pixels += static_cast<std::size_t>(count);
  }

  try {
    pending.committed_pixel_update.resize(update_pixels);
  } catch (const std::bad_alloc&) {
    error = "could not retain pending DRM pixel damage";
    return false;
  }
  auto* destination = pending.committed_pixel_update.data();
  for (const auto& rectangle : pending.committed_pixel_rectangles) {
    const auto x = static_cast<std::uint32_t>(rectangle.x);
    const auto y = static_cast<std::uint32_t>(rectangle.y);
    for (std::uint32_t row = 0; row < rectangle.height; ++row) {
      const auto* source =
          pixels.data() +
          static_cast<std::size_t>(y + row) * config_.output.width + x;
      std::memcpy(destination, source,
                  static_cast<std::size_t>(rectangle.width) *
                      sizeof(std::uint32_t));
      destination += rectangle.width;
    }
  }
  error.clear();
  return true;
}

void DrmPresenter::apply_committed_pixel_update(
    const PendingPresentation& pending) noexcept {
  if (pending.committed_pixel_update.empty())
    return;
  const auto* source = pending.committed_pixel_update.data();
  for (const auto& rectangle : pending.committed_pixel_rectangles) {
    const auto x = static_cast<std::uint32_t>(rectangle.x);
    const auto y = static_cast<std::uint32_t>(rectangle.y);
    for (std::uint32_t row = 0; row < rectangle.height; ++row) {
      auto* destination =
          committed_pixels_.data() +
          static_cast<std::size_t>(y + row) * config_.output.width + x;
      std::memcpy(destination, source,
                  static_cast<std::size_t>(rectangle.width) *
                      sizeof(std::uint32_t));
      source += rectangle.width;
    }
  }
}

}  // namespace glasswyrm::drm
