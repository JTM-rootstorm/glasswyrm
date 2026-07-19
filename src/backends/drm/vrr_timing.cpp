#include "backends/drm/vrr_timing.hpp"

#include <limits>

namespace glasswyrm::drm {

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

} // namespace glasswyrm::drm
