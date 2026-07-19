#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace glasswyrm::output::vrr {

inline constexpr std::size_t kTimingWindowCapacity = 512;
inline constexpr std::uint64_t kMinimumTimingToleranceNanoseconds = 250'000;
inline constexpr std::size_t kPositiveMinimumIntervalCount = 120;
inline constexpr std::uint32_t kPositiveMinimumPassBasisPoints = 7'500;
inline constexpr std::uint32_t kNegativeMaximumPassBasisPoints = 2'500;

enum class TimingObservation : std::uint8_t {
  BaselineAccepted,
  IntervalAccepted,
  TimestampUnavailable,
  SequenceRegression,
  TimestampRegression,
};

struct TimingSummary {
  std::size_t count{};
  std::uint64_t target_interval_nanoseconds{};
  std::uint64_t tolerance_nanoseconds{};
  std::size_t within_threshold_count{};
  std::uint64_t minimum_nanoseconds{};
  std::uint64_t maximum_nanoseconds{};
  std::uint64_t mean_nanoseconds{};
  std::uint64_t median_nanoseconds{};
  std::uint64_t p95_absolute_error_nanoseconds{};
  std::uint32_t pass_basis_points{};
};

class TimingStatistics {
public:
  explicit TimingStatistics(std::uint64_t target_interval_nanoseconds) noexcept;

  [[nodiscard]] TimingObservation
  observe(std::uint32_t sequence, std::uint64_t timestamp_nanoseconds,
          bool timestamp_available = true) noexcept;
  void reset() noexcept;

  [[nodiscard]] TimingSummary summary() const noexcept;
  [[nodiscard]] bool positive_evidence(bool effective_enabled) const noexcept;
  [[nodiscard]] bool negative_evidence(bool effective_enabled) const noexcept;

private:
  void append(std::uint64_t interval_nanoseconds) noexcept;

  std::uint64_t target_interval_nanoseconds_{};
  std::array<std::uint64_t, kTimingWindowCapacity> intervals_{};
  std::size_t begin_{};
  std::size_t count_{};
  bool has_previous_{};
  std::uint32_t previous_sequence_{};
  std::uint64_t previous_timestamp_nanoseconds_{};
};

[[nodiscard]] std::uint64_t
timing_tolerance(std::uint64_t target_interval_nanoseconds) noexcept;

} // namespace glasswyrm::output::vrr
