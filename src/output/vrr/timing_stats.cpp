#include "output/vrr/timing_stats.hpp"

#include <algorithm>

namespace glasswyrm::output::vrr {
namespace {

std::uint64_t absolute_difference(const std::uint64_t left,
                                  const std::uint64_t right) noexcept {
  return left >= right ? left - right : right - left;
}

bool sequence_follows(const std::uint32_t previous,
                      const std::uint32_t current) noexcept {
  const auto distance = static_cast<std::uint32_t>(current - previous);
  return distance != 0 && distance < (UINT32_C(1) << 31U);
}

std::uint64_t
bounded_mean(const std::array<std::uint64_t, kTimingWindowCapacity> &values,
             const std::size_t count) noexcept {
  if (count == 0)
    return 0;
  std::uint64_t quotients{};
  std::uint64_t remainders{};
  for (std::size_t index = 0; index < count; ++index) {
    quotients += values[index] / count;
    remainders += values[index] % count;
  }
  return quotients + remainders / count;
}

} // namespace

TimingStatistics::TimingStatistics(
    const std::uint64_t target_interval_nanoseconds) noexcept
    : target_interval_nanoseconds_(target_interval_nanoseconds) {}

TimingObservation
TimingStatistics::observe(const std::uint32_t sequence,
                          const std::uint64_t timestamp_nanoseconds,
                          const bool timestamp_available) noexcept {
  if (!timestamp_available || timestamp_nanoseconds == 0) {
    has_previous_ = false;
    return TimingObservation::TimestampUnavailable;
  }
  if (!has_previous_) {
    has_previous_ = true;
    previous_sequence_ = sequence;
    previous_timestamp_nanoseconds_ = timestamp_nanoseconds;
    return TimingObservation::BaselineAccepted;
  }
  if (!sequence_follows(previous_sequence_, sequence))
    return TimingObservation::SequenceRegression;
  if (timestamp_nanoseconds <= previous_timestamp_nanoseconds_)
    return TimingObservation::TimestampRegression;

  append(timestamp_nanoseconds - previous_timestamp_nanoseconds_);
  previous_sequence_ = sequence;
  previous_timestamp_nanoseconds_ = timestamp_nanoseconds;
  return TimingObservation::IntervalAccepted;
}

void TimingStatistics::reset() noexcept {
  begin_ = 0;
  count_ = 0;
  has_previous_ = false;
  previous_sequence_ = 0;
  previous_timestamp_nanoseconds_ = 0;
}

void TimingStatistics::append(
    const std::uint64_t interval_nanoseconds) noexcept {
  if (count_ < intervals_.size()) {
    intervals_[(begin_ + count_) % intervals_.size()] = interval_nanoseconds;
    ++count_;
    return;
  }
  intervals_[begin_] = interval_nanoseconds;
  begin_ = (begin_ + 1) % intervals_.size();
}

TimingSummary TimingStatistics::summary() const noexcept {
  TimingSummary result;
  result.count = count_;
  result.target_interval_nanoseconds = target_interval_nanoseconds_;
  result.tolerance_nanoseconds = timing_tolerance(target_interval_nanoseconds_);
  if (count_ == 0)
    return result;

  std::array<std::uint64_t, kTimingWindowCapacity> sorted{};
  std::array<std::uint64_t, kTimingWindowCapacity> errors{};
  for (std::size_t index = 0; index < count_; ++index) {
    const auto interval = intervals_[(begin_ + index) % intervals_.size()];
    sorted[index] = interval;
    errors[index] = absolute_difference(interval, target_interval_nanoseconds_);
    if (errors[index] <= result.tolerance_nanoseconds)
      ++result.within_threshold_count;
  }
  std::sort(sorted.begin(), sorted.begin() + count_);
  std::sort(errors.begin(), errors.begin() + count_);

  result.minimum_nanoseconds = sorted[0];
  result.maximum_nanoseconds = sorted[count_ - 1];
  result.mean_nanoseconds = bounded_mean(sorted, count_);
  const auto middle = count_ / 2;
  result.median_nanoseconds =
      count_ % 2 != 0
          ? sorted[middle]
          : sorted[middle - 1] + (sorted[middle] - sorted[middle - 1]) / 2;
  const auto p95_rank = (count_ * 95U + 99U) / 100U;
  result.p95_absolute_error_nanoseconds = errors[p95_rank - 1];
  result.pass_basis_points = static_cast<std::uint32_t>(
      result.within_threshold_count * UINT64_C(10'000) / count_);
  return result;
}

bool TimingStatistics::positive_evidence(
    const bool effective_enabled) const noexcept {
  const auto value = summary();
  return target_interval_nanoseconds_ != 0 && effective_enabled &&
         value.count >= kPositiveMinimumIntervalCount &&
         value.pass_basis_points >= kPositiveMinimumPassBasisPoints;
}

bool TimingStatistics::negative_evidence(
    const bool effective_enabled) const noexcept {
  const auto value = summary();
  return target_interval_nanoseconds_ != 0 && !effective_enabled &&
         value.count >= kPositiveMinimumIntervalCount &&
         value.pass_basis_points < kNegativeMaximumPassBasisPoints;
}

std::uint64_t
timing_tolerance(const std::uint64_t target_interval_nanoseconds) noexcept {
  return std::max(kMinimumTimingToleranceNanoseconds,
                  target_interval_nanoseconds / 100U);
}

} // namespace glasswyrm::output::vrr
