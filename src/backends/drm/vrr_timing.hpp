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

struct VrrTimestampResult {
  VrrTimestampStatus status{VrrTimestampStatus::ArithmeticOverflow};
  std::uint64_t nanoseconds{};
};

[[nodiscard]] VrrTimestampResult convert_page_flip_timestamp(
    std::uint64_t seconds, std::uint64_t microseconds,
    std::optional<std::uint64_t> previous_nanoseconds = std::nullopt) noexcept;

} // namespace glasswyrm::drm
