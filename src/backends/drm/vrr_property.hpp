#pragma once

#include "backends/drm/kms_api.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace glasswyrm::drm {

enum class VrrPropertyStatus {
  Absent,
  Success,
  Duplicate,
  InvalidId,
  InvalidWidth,
  InvalidRange,
  InvalidValue,
};

struct VrrPropertyDiscovery {
  VrrPropertyStatus status{VrrPropertyStatus::Absent};
  PropertyBinding binding;
};

[[nodiscard]] VrrPropertyDiscovery discover_vrr_enabled_property(
    std::span<const ObjectProperty> crtc_properties) noexcept;

[[nodiscard]] bool test_vrr_controllability(
    KmsApi &api, int fd, bool connector_vrr_capable, std::uint32_t crtc_id,
    const std::optional<PropertyBinding> &vrr_enabled,
    std::span<const AtomicPropertyValue> selected_state, std::string &error);

} // namespace glasswyrm::drm
