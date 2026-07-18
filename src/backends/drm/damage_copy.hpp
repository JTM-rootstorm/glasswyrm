#pragma once

#include "compositor/rectangle.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string_view>
#include <vector>

namespace glasswyrm::drm {

enum class FullCopyReason {
  None,
  FirstUse,
  HistoryMiss,
  DamageUnavailable,
  CanonicalMismatch,
  VirtualTerminalResume,
  OutputConfigurationChanged,
};

[[nodiscard]] std::string_view full_copy_reason_name(
    FullCopyReason reason) noexcept;

struct DamageCopyPlan {
  std::vector<gw::compositor::Rectangle> rectangles;
  std::vector<gw::compositor::Rectangle> completed_damage;
  std::uint64_t full_frame_bytes{};
  std::uint64_t copied_bytes{};
  std::uint64_t history_span{};
  FullCopyReason full_copy_reason{FullCopyReason::None};

  [[nodiscard]] bool full_copy() const noexcept {
    return full_copy_reason != FullCopyReason::None;
  }
};

class DamageCopyHistory final {
 public:
  static constexpr std::size_t kMaximumCompletedFrames = 8;

  DamageCopyHistory(std::uint32_t width, std::uint32_t height) noexcept;

  [[nodiscard]] DamageCopyPlan plan(
      bool buffer_content_valid, std::uint64_t buffer_generation,
      std::uint64_t target_generation,
      std::span<const gw::compositor::Rectangle> current_damage,
      FullCopyReason forced_reason = FullCopyReason::None) const;
  void record_completed(const DamageCopyPlan& plan,
                        std::uint64_t generation);
  void clear() noexcept;

 private:
  struct CompletedDamage {
    std::uint64_t generation{};
    std::vector<gw::compositor::Rectangle> rectangles;
  };

  [[nodiscard]] DamageCopyPlan full_plan(
      FullCopyReason reason,
      std::span<const gw::compositor::Rectangle> completed_damage) const;
  [[nodiscard]] bool append_history(
      std::uint64_t first_generation, std::uint64_t last_generation,
      std::vector<gw::compositor::Rectangle>& rectangles) const;

  std::uint32_t width_{};
  std::uint32_t height_{};
  std::deque<CompletedDamage> completed_;
};

}  // namespace glasswyrm::drm
