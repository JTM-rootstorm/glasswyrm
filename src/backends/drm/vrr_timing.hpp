#pragma once

#include <cstdint>
#include <optional>
#include <span>

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
  LegacyVBlankQuery,
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

struct CrtcIndexBinding {
  std::uint32_t crtc_id{};
  std::uint32_t crtc_index{};
};

enum class LegacyVBlankCounterStatus : std::uint8_t {
  Success,
  Duplicate,
  Regression,
  ArithmeticOverflow,
};

struct LegacyVBlankCounterState {
  std::uint32_t raw_sequence{};
  std::uint64_t extended_sequence{};
};

struct LegacyVBlankCounterResult {
  LegacyVBlankCounterStatus status{LegacyVBlankCounterStatus::Regression};
  LegacyVBlankCounterState state;
};

inline constexpr std::uint32_t kLegacyVBlankHighCrtcShift = 1;
inline constexpr std::uint32_t kLegacyVBlankHighCrtcMask = UINT32_C(0x3e);

[[nodiscard]] std::optional<std::uint32_t>
find_crtc_index(std::span<const CrtcIndexBinding> bindings,
                std::uint32_t crtc_id) noexcept;

[[nodiscard]] std::optional<std::uint32_t>
legacy_vblank_crtc_selector(std::uint32_t crtc_index) noexcept;

[[nodiscard]] LegacyVBlankCounterResult extend_legacy_vblank_counter(
    std::uint32_t raw_sequence,
    std::optional<LegacyVBlankCounterState> previous = std::nullopt) noexcept;

[[nodiscard]] VrrTimestampResult convert_page_flip_timestamp(
    std::uint64_t seconds, std::uint64_t microseconds,
    std::optional<std::uint64_t> previous_nanoseconds = std::nullopt) noexcept;

[[nodiscard]] CrtcSequenceSample assess_crtc_sequence_sample(
    std::uint64_t event_timestamp_nanoseconds, bool event_timestamp_available,
    bool query_succeeded, bool timestamp_monotonic,
    std::uint64_t query_sequence, std::uint64_t query_timestamp_nanoseconds,
    std::optional<CrtcSequencePoint> previous = std::nullopt,
    VrrTimingSource source = VrrTimingSource::CrtcSequenceQuery) noexcept;

} // namespace glasswyrm::drm
