#pragma once

#include "glasswyrmd/window.hpp"

#include <cstdint>

namespace glasswyrm::server {

inline constexpr std::uint8_t kGwScalePreferredReason = 1U << 0U;
inline constexpr std::uint8_t kGwScaleMembershipReason = 1U << 1U;
inline constexpr std::uint8_t kGwScaleInvalidatedReason = 1U << 2U;

[[nodiscard]] bool invalidate_scaled_pixmap(WindowScaleState& scale) noexcept;
[[nodiscard]] std::uint8_t scale_notification_reasons(
    const WindowScaleState& before, const WindowScaleState& after) noexcept;

}  // namespace glasswyrm::server
