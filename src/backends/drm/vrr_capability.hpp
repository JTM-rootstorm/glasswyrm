#pragma once

#include <cstdint>
#include <span>

namespace glasswyrm::drm {

enum class ConnectorVrrCapabilityStatus {
  Absent,
  NotCapable,
  Capable,
  Malformed,
};

struct ConnectorVrrCapability {
  ConnectorVrrCapabilityStatus status{ConnectorVrrCapabilityStatus::Absent};
  bool property_present{};
  bool capable{};
};

[[nodiscard]] ConnectorVrrCapability classify_connector_vrr_capability(
    std::span<const std::uint64_t> property_values) noexcept;

} // namespace glasswyrm::drm
