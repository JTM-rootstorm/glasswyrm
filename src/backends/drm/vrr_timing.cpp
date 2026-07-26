#include "backends/drm/vrr_timing.hpp"

#include <limits>

namespace glasswyrm::drm {

std::optional<std::uint32_t>
find_crtc_index(const std::span<const CrtcIndexBinding> bindings,
                const std::uint32_t crtc_id) noexcept {
  for (const auto& binding : bindings)
    if (binding.crtc_id == crtc_id)
      return binding.crtc_index;
  return std::nullopt;
}

std::optional<std::uint32_t>
legacy_vblank_crtc_selector(const std::uint32_t crtc_index) noexcept {
  constexpr auto maximum_index =
      kLegacyVBlankHighCrtcMask >> kLegacyVBlankHighCrtcShift;
  if (crtc_index > maximum_index)
    return std::nullopt;
  return crtc_index << kLegacyVBlankHighCrtcShift;
}

LegacyVBlankCounterResult extend_legacy_vblank_counter(
    const std::uint32_t raw_sequence,
    const std::optional<LegacyVBlankCounterState> previous) noexcept {
  if (!previous)
    return {LegacyVBlankCounterStatus::Success,
            {raw_sequence, raw_sequence}};

  const std::uint32_t delta = raw_sequence - previous->raw_sequence;
  if (delta == 0)
    return {LegacyVBlankCounterStatus::Duplicate, *previous};
  if (delta > static_cast<std::uint32_t>(
                  std::numeric_limits<std::int32_t>::max()))
    return {LegacyVBlankCounterStatus::Regression, *previous};
  if (delta >
      std::numeric_limits<std::uint64_t>::max() -
          previous->extended_sequence)
    return {LegacyVBlankCounterStatus::ArithmeticOverflow, *previous};
  return {
      LegacyVBlankCounterStatus::Success,
      {raw_sequence, previous->extended_sequence + delta},
  };
}

VrrTimestampResult convert_page_flip_timestamp(
    const std::uint64_t seconds, const std::uint64_t microseconds,
    const std::optional<std::uint64_t> previous_nanoseconds) noexcept {
  constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000ULL;
  constexpr std::uint64_t nanoseconds_per_microsecond = 1'000ULL;
  if (microseconds >= 1'000'000ULL)
    return {VrrTimestampStatus::InvalidMicroseconds, 0};
  if (seconds >
      std::numeric_limits<std::uint64_t>::max() / nanoseconds_per_second)
    return {VrrTimestampStatus::ArithmeticOverflow, 0};
  const auto second_part = seconds * nanoseconds_per_second;
  const auto microsecond_part = microseconds * nanoseconds_per_microsecond;
  if (microsecond_part >
      std::numeric_limits<std::uint64_t>::max() - second_part)
    return {VrrTimestampStatus::ArithmeticOverflow, 0};
  const auto value = second_part + microsecond_part;
  if (previous_nanoseconds && value < *previous_nanoseconds)
    return {VrrTimestampStatus::Regression, value};
  return {VrrTimestampStatus::Success, value};
}

CrtcSequenceSample assess_crtc_sequence_sample(
    const std::uint64_t event_timestamp_nanoseconds,
    const bool event_timestamp_available, const bool query_succeeded,
    const bool timestamp_monotonic, const std::uint64_t query_sequence,
    const std::uint64_t query_timestamp_nanoseconds,
    const std::optional<CrtcSequencePoint> previous,
    const VrrTimingSource source) noexcept {
  CrtcSequenceSample result;
  result.source = source;
  result.sequence = query_sequence;
  result.timestamp_nanoseconds = query_timestamp_nanoseconds;
  if (!query_succeeded) {
    result.correlation = CrtcSequenceCorrelation::QueryFailed;
    return result;
  }
  if (!timestamp_monotonic || query_sequence == 0 ||
      query_timestamp_nanoseconds == 0) {
    result.correlation = CrtcSequenceCorrelation::Invalid;
    return result;
  }
  constexpr std::uint64_t event_quantization_nanoseconds = 1'000;
  if (!event_timestamp_available || event_timestamp_nanoseconds == 0 ||
      query_timestamp_nanoseconds < event_timestamp_nanoseconds ||
      query_timestamp_nanoseconds - event_timestamp_nanoseconds >=
          event_quantization_nanoseconds) {
    result.correlation = CrtcSequenceCorrelation::Uncorrelated;
    return result;
  }
  if (previous &&
      (query_sequence <= previous->sequence ||
       query_timestamp_nanoseconds <= previous->timestamp_nanoseconds)) {
    result.correlation = CrtcSequenceCorrelation::NonMonotonic;
    return result;
  }
  result.correlation = CrtcSequenceCorrelation::Correlated;
  result.cadence_eligible = true;
  return result;
}

} // namespace glasswyrm::drm
