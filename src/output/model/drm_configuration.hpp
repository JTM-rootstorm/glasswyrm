#pragma once

#include "output/model/types.hpp"

namespace glasswyrm::output {

enum class DrmConfigurationError {
  None,
  NotSingleFixedOutput,
  OutputIdentityChanged,
  PhysicalModeChanged,
  LogicalPositionChanged,
  PrimaryStateChanged,
  EnabledStateChanged,
};

[[nodiscard]] bool
is_single_fixed_drm_output_profile(const OutputLayout &layout) noexcept;

[[nodiscard]] DrmConfigurationError validate_single_fixed_drm_configuration(
    const OutputLayout &committed, const OutputLayout &candidate) noexcept;

[[nodiscard]] const char *
drm_configuration_error_name(DrmConfigurationError error) noexcept;

} // namespace glasswyrm::output
