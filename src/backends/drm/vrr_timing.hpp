#pragma once

#include <cstdint>
#include <optional>

namespace glasswyrm::drm {

enum class VrrTimestampStatus {
  Success,
  InvalidMicroseconds,
  ArithmeticOverflow,
  Regression,
};

enum class VrrTimingSource : std::uint8_t {
  PageFlipEvent,
  CrtcSequenceQuery,
};

enum class CrtcSequenceCorrelation : std::uint8_t {
  NotSampled,
  QueryFailed,
  Invalid,
  Uncorrelated,
  NonMonotonic,
  Correlated,
};

struct VrrTimestampResult {
  VrrTimestampStatus status{VrrTimestampStatus::ArithmeticOverflow};
  std::uint64_t nanoseconds{};
};

struct CrtcSequencePoint {
  std::uint64_t sequence{};
  std::uint64_t timestamp_nanoseconds{};
};

struct CrtcSequenceSample {
  VrrTimingSource source{VrrTimingSource::CrtcSequenceQuery};
  CrtcSequenceCorrelation correlation{CrtcSequenceCorrelation::NotSampled};
  std::uint64_t sequence{};
  std::uint64_t timestamp_nanoseconds{};
  bool cadence_eligible{};
};

[[nodiscard]] VrrTimestampResult convert_page_flip_timestamp(
    std::uint64_t seconds, std::uint64_t microseconds,
    std::optional<std::uint64_t> previous_nanoseconds = std::nullopt) noexcept;

[[nodiscard]] CrtcSequenceSample assess_crtc_sequence_sample(
    std::uint64_t event_timestamp_nanoseconds, bool event_timestamp_available,
    bool query_succeeded, bool timestamp_monotonic,
    std::uint64_t query_sequence, std::uint64_t query_timestamp_nanoseconds,
    std::optional<CrtcSequencePoint> previous = std::nullopt) noexcept;

} // namespace glasswyrm::drm
