#include "backends/drm/damage_copy.hpp"

#include "compositor/damage_region.hpp"

#include <limits>

namespace glasswyrm::drm {
namespace {

using gw::compositor::DamageRegion;
using gw::compositor::Rectangle;

std::uint64_t byte_count(std::span<const Rectangle> rectangles) noexcept {
  std::uint64_t total = 0;
  for (const auto& rectangle : rectangles) {
    const auto pixels = std::uint64_t{rectangle.width} * rectangle.height;
    if (pixels > (std::numeric_limits<std::uint64_t>::max() - total) / 4U)
      return std::numeric_limits<std::uint64_t>::max();
    total += pixels * 4U;
  }
  return total;
}

}  // namespace

std::string_view full_copy_reason_name(const FullCopyReason reason) noexcept {
  switch (reason) {
    case FullCopyReason::None: return "none";
    case FullCopyReason::FirstUse: return "first-use";
    case FullCopyReason::HistoryMiss: return "history-miss";
    case FullCopyReason::DamageUnavailable: return "damage-unavailable";
    case FullCopyReason::CanonicalMismatch: return "canonical-mismatch";
    case FullCopyReason::VirtualTerminalResume: return "vt-resume";
  }
  return "unknown";
}

DamageCopyHistory::DamageCopyHistory(const std::uint32_t width,
                                     const std::uint32_t height) noexcept
    : width_(width), height_(height) {}

DamageCopyPlan DamageCopyHistory::full_plan(
    const FullCopyReason reason,
    const std::span<const Rectangle> completed_damage) const {
  DamageCopyPlan result;
  result.full_frame_bytes = std::uint64_t{width_} * height_ * 4U;
  result.copied_bytes = result.full_frame_bytes;
  result.full_copy_reason = reason;
  if (width_ != 0 && height_ != 0)
    result.rectangles.push_back({0, 0, width_, height_});
  DamageRegion normalized({0, 0, width_, height_});
  if (completed_damage.empty() ||
      reason == FullCopyReason::CanonicalMismatch ||
      reason == FullCopyReason::VirtualTerminalResume) {
    normalized.add_full_output();
  } else {
    for (const auto& rectangle : completed_damage) normalized.add(rectangle);
  }
  result.completed_damage = normalized.rectangles();
  return result;
}

bool DamageCopyHistory::append_history(
    const std::uint64_t first_generation,
    const std::uint64_t last_generation,
    std::vector<Rectangle>& rectangles) const {
  if (first_generation > last_generation) return true;
  auto expected = first_generation;
  for (const auto& entry : completed_) {
    if (entry.generation < first_generation) continue;
    if (entry.generation > last_generation) break;
    if (entry.generation != expected) return false;
    rectangles.insert(rectangles.end(), entry.rectangles.begin(),
                      entry.rectangles.end());
    ++expected;
  }
  return expected == last_generation + 1U;
}

DamageCopyPlan DamageCopyHistory::plan(
    const bool buffer_content_valid, const std::uint64_t buffer_generation,
    const std::uint64_t target_generation,
    const std::span<const Rectangle> current_damage,
    const FullCopyReason forced_reason) const {
  if (forced_reason != FullCopyReason::None)
    return full_plan(forced_reason, current_damage);
  if (!buffer_content_valid)
    return full_plan(FullCopyReason::FirstUse, current_damage);
  if (target_generation <= buffer_generation)
    return full_plan(FullCopyReason::HistoryMiss, current_damage);
  if (current_damage.empty())
    return full_plan(FullCopyReason::DamageUnavailable, current_damage);

  std::vector<Rectangle> missing_damage;
  if (!append_history(buffer_generation + 1U, target_generation - 1U,
                      missing_damage))
    return full_plan(FullCopyReason::HistoryMiss, current_damage);
  missing_damage.insert(missing_damage.end(), current_damage.begin(),
                        current_damage.end());

  DamageRegion normalized({0, 0, width_, height_});
  for (const auto& rectangle : missing_damage) normalized.add(rectangle);
  if (normalized.empty())
    return full_plan(FullCopyReason::DamageUnavailable, current_damage);

  DamageCopyPlan result;
  result.rectangles = normalized.rectangles();
  result.full_frame_bytes = std::uint64_t{width_} * height_ * 4U;
  result.copied_bytes = byte_count(result.rectangles);
  result.history_span = target_generation - buffer_generation;
  DamageRegion completed({0, 0, width_, height_});
  for (const auto& rectangle : current_damage) completed.add(rectangle);
  result.completed_damage = completed.rectangles();
  return result;
}

void DamageCopyHistory::record_completed(const DamageCopyPlan& plan,
                                         const std::uint64_t generation) {
  CompletedDamage value;
  value.generation = generation;
  value.rectangles = plan.completed_damage;
  if (!completed_.empty() && completed_.back().generation >= generation)
    clear();
  completed_.push_back(std::move(value));
  while (completed_.size() > kMaximumCompletedFrames) completed_.pop_front();
}

void DamageCopyHistory::clear() noexcept { completed_.clear(); }

}  // namespace glasswyrm::drm
