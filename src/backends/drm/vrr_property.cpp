#include "backends/drm/vrr_property.hpp"

#include <vector>

namespace glasswyrm::drm {

VrrPropertyDiscovery discover_vrr_enabled_property(
    const std::span<const ObjectProperty> crtc_properties) noexcept {
  const ObjectProperty *found = nullptr;
  for (const auto &property : crtc_properties) {
    if (property.name != "VRR_ENABLED")
      continue;
    if (found != nullptr)
      return {VrrPropertyStatus::Duplicate, {}};
    found = &property;
  }
  if (found == nullptr)
    return {};
  if (found->id == 0)
    return {VrrPropertyStatus::InvalidId, {}};
  if (found->value_width_bits == 0 || found->value_width_bits > 64)
    return {VrrPropertyStatus::InvalidWidth, {}};
  if (!found->range || found->range->minimum != 0 || found->range->maximum != 1)
    return {VrrPropertyStatus::InvalidRange, {}};
  if (found->value > 1)
    return {VrrPropertyStatus::InvalidValue, {}};
  return {VrrPropertyStatus::Success,
          {found->id, found->value, found->value_width_bits, found->range}};
}

bool test_vrr_controllability(
    KmsApi &api, const int fd, const bool connector_vrr_capable,
    const std::uint32_t crtc_id,
    const std::optional<PropertyBinding> &vrr_enabled,
    const std::span<const AtomicPropertyValue> selected_state,
    std::string &error) {
  if (!connector_vrr_capable) {
    error = "DRM connector is not VRR capable";
    return false;
  }
  if (!vrr_enabled) {
    error = "CRTC VRR_ENABLED property is absent";
    return false;
  }
  if (crtc_id == 0 || vrr_enabled->id == 0 || !vrr_enabled->range ||
      vrr_enabled->range->minimum != 0 || vrr_enabled->range->maximum != 1) {
    error = "VRR_ENABLED property binding is invalid";
    return false;
  }
  std::vector<AtomicPropertyValue> request(selected_state.begin(),
                                           selected_state.end());
  AtomicPropertyValue *vrr_value = nullptr;
  for (auto &property : request) {
    if (property.object_id == crtc_id &&
        property.property_id == vrr_enabled->id) {
      if (vrr_value != nullptr) {
        error = "selected atomic state duplicates VRR_ENABLED";
        return false;
      }
      vrr_value = &property;
    }
  }
  if (vrr_value == nullptr) {
    request.push_back({crtc_id, vrr_enabled->id, 0});
    vrr_value = &request.back();
  } else {
    vrr_value->value = 0;
  }
  std::string operation_error;
  constexpr auto flags = AtomicTestOnly | AtomicAllowModeset;
  if (!api.atomic_commit(fd, request, flags, nullptr, operation_error)) {
    error = "VRR_ENABLED=0 TEST_ONLY failed: " + operation_error;
    return false;
  }
  vrr_value->value = 1;
  if (!api.atomic_commit(fd, request, flags, nullptr, operation_error)) {
    error = "VRR_ENABLED=1 TEST_ONLY failed: " + operation_error;
    return false;
  }
  error.clear();
  return true;
}

} // namespace glasswyrm::drm
