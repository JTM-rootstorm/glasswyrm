#pragma once

#include "backends/drm/kms_vrr_state.hpp"
#include "backends/output/presentation_backend.hpp"
#include "output/vrr/timing_stats.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace glasswyrm::drm {

struct PresenterVrrPlan {
  bool accepted{};
  bool desired_enabled{};
  bool include_property{};
  bool requires_flip{};
  std::string error;
};

class PresenterVrrState {
public:
  void initialize(std::uint64_t output_id, KmsVrrState kms_state,
                  bool timestamp_monotonic,
                  std::uint32_t refresh_millihertz) noexcept;

  [[nodiscard]] PresenterVrrPlan
  plan(const output::VrrPresentationRequest &request,
       bool explicit_reaffirmation = false) const;

  void complete_initial(bool readback_enabled, bool readback_valid) noexcept;
  void complete_flip(bool desired_enabled, bool readback_enabled,
                     bool readback_valid,
                     std::uint32_t sequence,
                     std::uint64_t kernel_timestamp_nanoseconds,
                     bool timestamp_available) noexcept;
  void mark_suspended_off() noexcept;
  void mark_acquired_off() noexcept;
  void mark_session_active() noexcept;
  void mark_restored() noexcept;

  [[nodiscard]] output::VrrPresentationCapability capability(
      bool output_enabled, bool connected) const noexcept;
  [[nodiscard]] output::VrrPresentationFeedback feedback() const noexcept;
  [[nodiscard]] output::vrr::TimingSummary timing_summary() const noexcept;
  [[nodiscard]] std::size_t enabled_period_count() const noexcept {
    return enabled_period_count_;
  }
  [[nodiscard]] std::size_t disabled_period_count() const noexcept {
    return disabled_period_count_;
  }

  [[nodiscard]] const KmsVrrState &kms_state() const noexcept {
    return kms_state_;
  }
  [[nodiscard]] bool effective_enabled() const noexcept {
    return effective_enabled_;
  }
  [[nodiscard]] bool session_active() const noexcept { return session_active_; }
  [[nodiscard]] std::uint64_t target_interval_nanoseconds() const noexcept {
    return target_interval_nanoseconds_;
  }

private:
  std::uint64_t output_id_{};
  KmsVrrState kms_state_;
  bool timestamp_monotonic_{};
  bool effective_enabled_{};
  bool property_readback_valid_{};
  bool session_active_{true};
  std::uint32_t flip_sequence_{};
  std::uint64_t kernel_timestamp_nanoseconds_{};
  std::uint64_t interval_nanoseconds_{};
  std::uint64_t target_interval_nanoseconds_{};
  bool timestamp_available_{};
  std::optional<output::vrr::TimingStatistics> timing_statistics_;
  std::size_t enabled_period_count_{};
  std::size_t disabled_period_count_{};
};

} // namespace glasswyrm::drm
