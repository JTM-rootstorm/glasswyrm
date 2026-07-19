#include "backends/drm/vrr_capability.hpp"

namespace glasswyrm::drm {

ConnectorVrrCapability classify_connector_vrr_capability(
    const std::span<const std::uint64_t> property_values) noexcept {
  if (property_values.empty())
    return {};
  if (property_values.size() != 1 || property_values.front() > 1)
    return {ConnectorVrrCapabilityStatus::Malformed, true, false};
  if (property_values.front() == 0)
    return {ConnectorVrrCapabilityStatus::NotCapable, true, false};
  return {ConnectorVrrCapabilityStatus::Capable, true, true};
}

} // namespace glasswyrm::drm
