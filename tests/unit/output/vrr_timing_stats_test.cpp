#include "output/vrr/timing_stats.hpp"

#include "helpers/test_support.hpp"

#include <cstdint>

namespace {

using namespace glasswyrm::output::vrr;
using gw::test::require;

void test_observation_validation_and_wrap() {
  TimingStatistics statistics{1'000'000};
  require(statistics.observe(10, 1'000'000) ==
                  TimingObservation::BaselineAccepted &&
              statistics.observe(11, 2'000'000) ==
                  TimingObservation::IntervalAccepted,
          "consecutive valid events produce an interval");
  require(statistics.observe(10, 3'000'000) ==
                  TimingObservation::SequenceRegression &&
              statistics.observe(12, 1'500'000) ==
                  TimingObservation::TimestampRegression &&
              statistics.summary().count == 1,
          "sequence and timestamp regression do not mutate statistics");

  require(statistics.observe(12, 0, false) ==
                  TimingObservation::TimestampUnavailable &&
              statistics.observe(20, 9'000'000) ==
                  TimingObservation::BaselineAccepted &&
              statistics.summary().count == 1,
          "unavailable timestamps break the consecutive-event baseline");

  statistics.reset();
  require(statistics.observe(UINT32_MAX, 100) ==
                  TimingObservation::BaselineAccepted &&
              statistics.observe(0, 200) ==
                  TimingObservation::IntervalAccepted &&
              statistics.summary().count == 1,
          "page-flip sequence wrap is forward progress");
}

void test_exact_summary() {
  TimingStatistics statistics{1'000'000};
  std::uint64_t timestamp = 1;
  require(statistics.observe(1, timestamp) ==
              TimingObservation::BaselineAccepted,
          "seed exact statistics");
  for (const auto interval : {UINT64_C(750'000), UINT64_C(1'000'000),
                              UINT64_C(1'250'000), UINT64_C(2'000'000)}) {
    timestamp += interval;
    require(statistics.observe(
                static_cast<std::uint32_t>(statistics.summary().count + 2),
                timestamp) == TimingObservation::IntervalAccepted,
            "append exact statistics sample");
  }
  const auto value = statistics.summary();
  require(value.count == 4 && value.target_interval_nanoseconds == 1'000'000 &&
              value.tolerance_nanoseconds == 250'000 &&
              value.within_threshold_count == 3 &&
              value.minimum_nanoseconds == 750'000 &&
              value.maximum_nanoseconds == 2'000'000 &&
              value.mean_nanoseconds == 1'250'000 &&
              value.median_nanoseconds == 1'125'000 &&
              value.p95_absolute_error_nanoseconds == 1'000'000 &&
              value.pass_basis_points == 7'500,
          "summary statistics use exact deterministic integer rules");
  require(timing_tolerance(16'666'667) == 250'000 &&
              timing_tolerance(100'000'000) == 1'000'000,
          "tolerance is max of 250 microseconds and one percent");
}

void test_bounded_ring() {
  TimingStatistics statistics{1'000};
  std::uint64_t timestamp = 1;
  require(statistics.observe(0, timestamp) ==
              TimingObservation::BaselineAccepted,
          "seed bounded ring");
  for (std::uint32_t interval = 1; interval <= 513; ++interval) {
    timestamp += interval;
    require(statistics.observe(interval, timestamp) ==
                TimingObservation::IntervalAccepted,
            "append bounded ring interval");
  }
  const auto value = statistics.summary();
  require(value.count == kTimingWindowCapacity &&
              value.minimum_nanoseconds == 2 &&
              value.maximum_nanoseconds == 513 &&
              value.mean_nanoseconds == 257 && value.median_nanoseconds == 257,
          "rolling ring retains only the newest 512 intervals");
}

void append_constant(TimingStatistics &statistics, const std::uint64_t interval,
                     const std::size_t count) {
  std::uint64_t timestamp = 1;
  require(statistics.observe(0, timestamp) ==
              TimingObservation::BaselineAccepted,
          "seed evidence run");
  for (std::size_t index = 0; index < count; ++index) {
    timestamp += interval;
    require(
        statistics.observe(static_cast<std::uint32_t>(index + 1), timestamp) ==
            TimingObservation::IntervalAccepted,
        "append evidence interval");
  }
}

void test_acceptance_thresholds() {
  TimingStatistics invalid_target{0};
  append_constant(invalid_target, 1, 120);
  require(!invalid_target.positive_evidence(true) &&
              !invalid_target.negative_evidence(false),
          "zero target interval cannot produce acceptance evidence");

  TimingStatistics positive{10'000'000};
  append_constant(positive, 10'000'000, 120);
  require(positive.positive_evidence(true) &&
              !positive.positive_evidence(false) &&
              !positive.negative_evidence(false),
          "positive evidence requires enabled readback, 120 samples, and 75 "
          "percent");

  TimingStatistics insufficient{10'000'000};
  append_constant(insufficient, 10'000'000, 119);
  require(!insufficient.positive_evidence(true),
          "positive evidence rejects fewer than 120 intervals");

  TimingStatistics negative{10'000'000};
  append_constant(negative, 11'000'000, 120);
  require(
      negative.negative_evidence(false) && !negative.negative_evidence(true) &&
          !negative.positive_evidence(true),
      "negative evidence requires disabled readback and less than 25 percent");
}

} // namespace

int main() {
  test_observation_validation_and_wrap();
  test_exact_summary();
  test_bounded_ring();
  test_acceptance_thresholds();
  return 0;
}
