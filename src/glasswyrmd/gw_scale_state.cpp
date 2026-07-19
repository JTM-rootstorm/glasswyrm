#include "glasswyrmd/gw_scale_state.hpp"

namespace glasswyrm::server {

bool invalidate_scaled_pixmap(WindowScaleState& scale) noexcept {
  if (scale.presentation != WindowScalePresentationState::ScaleAwareActive)
    return false;
  scale.presentation =
      WindowScalePresentationState::ScaleAwareAwaitingPixmap;
  scale.scaled_pixmap_storage.reset();
  scale.presentation_serial = 0;
  return true;
}

std::uint8_t scale_notification_reasons(
    const WindowScaleState& before, const WindowScaleState& after) noexcept {
  std::uint8_t reasons = 0;
  if (before.primary_output != after.primary_output ||
      before.preferred_scale_numerator != after.preferred_scale_numerator ||
      before.preferred_scale_denominator != after.preferred_scale_denominator)
    reasons |= kGwScalePreferredReason;
  if (before.output_memberships != after.output_memberships)
    reasons |= kGwScaleMembershipReason;
  if (before.presentation == WindowScalePresentationState::ScaleAwareActive &&
      after.presentation != WindowScalePresentationState::ScaleAwareActive)
    reasons |= kGwScaleInvalidatedReason;
  return reasons;
}

}  // namespace glasswyrm::server
