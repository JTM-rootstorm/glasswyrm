#include "output/model/drm_configuration.hpp"

namespace glasswyrm::output {

bool is_single_fixed_drm_output_profile(
    const OutputLayout &layout) noexcept {
  if (layout.descriptors.size() != 1 || layout.states.size() != 1 ||
      layout.output_order.size() != 1)
    return false;
  const auto id = layout.output_order.front();
  const auto descriptor = layout.descriptors.find(id);
  const auto state = layout.states.find(id);
  return descriptor != layout.descriptors.end() &&
         state != layout.states.end() && descriptor->second.id == id &&
         state->second.output_id == id &&
         descriptor->second.kind == OutputKind::Drm &&
         !descriptor->second.mode_configurable &&
         descriptor->second.scale_configurable &&
         descriptor->second.transform_configurable;
}

DrmConfigurationError validate_single_fixed_drm_configuration(
    const OutputLayout &committed, const OutputLayout &candidate) noexcept {
  if (!is_single_fixed_drm_output_profile(committed) ||
      !is_single_fixed_drm_output_profile(candidate))
    return DrmConfigurationError::NotSingleFixedOutput;
  const auto committed_id = committed.output_order.front();
  const auto candidate_id = candidate.output_order.front();
  if (committed_id != candidate_id)
    return DrmConfigurationError::OutputIdentityChanged;

  const auto &before = committed.states.at(committed_id);
  const auto &after = candidate.states.at(candidate_id);
  if (before.mode_id != after.mode_id ||
      before.physical_width != after.physical_width ||
      before.physical_height != after.physical_height ||
      before.refresh_millihertz != after.refresh_millihertz)
    return DrmConfigurationError::PhysicalModeChanged;
  if (before.logical_x != after.logical_x ||
      before.logical_y != after.logical_y)
    return DrmConfigurationError::LogicalPositionChanged;
  if (before.primary != after.primary ||
      committed.primary_output_id != candidate.primary_output_id)
    return DrmConfigurationError::PrimaryStateChanged;
  if (before.enabled != after.enabled)
    return DrmConfigurationError::EnabledStateChanged;
  return DrmConfigurationError::None;
}

const char *
drm_configuration_error_name(const DrmConfigurationError error) noexcept {
  switch (error) {
  case DrmConfigurationError::None:
    return "none";
  case DrmConfigurationError::NotSingleFixedOutput:
    return "not-single-fixed-output";
  case DrmConfigurationError::OutputIdentityChanged:
    return "output-identity-changed";
  case DrmConfigurationError::PhysicalModeChanged:
    return "physical-mode-changed";
  case DrmConfigurationError::LogicalPositionChanged:
    return "logical-position-changed";
  case DrmConfigurationError::PrimaryStateChanged:
    return "primary-state-changed";
  case DrmConfigurationError::EnabledStateChanged:
    return "enabled-state-changed";
  }
  return "unknown";
}

} // namespace glasswyrm::output
