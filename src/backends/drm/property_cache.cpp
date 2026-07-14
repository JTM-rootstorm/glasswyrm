#include "backends/drm/property_cache.hpp"

#include <array>
#include <string_view>

namespace glasswyrm::drm {
namespace {

struct RequiredProperty {
  std::string_view name;
  PropertyBinding* destination;
};

PropertyCacheStatus cache_required(
    const std::span<const ObjectProperty> properties,
    const std::span<const RequiredProperty> required,
    std::string& failed_name) {
  for (const auto& requirement : required) {
    const ObjectProperty* found = nullptr;
    for (const auto& property : properties) {
      if (property.name != requirement.name) continue;
      if (found != nullptr) {
        failed_name = requirement.name;
        return PropertyCacheStatus::DuplicateProperty;
      }
      found = &property;
    }
    if (found == nullptr) {
      failed_name = requirement.name;
      return PropertyCacheStatus::MissingProperty;
    }
    if (found->id == 0) {
      failed_name = requirement.name;
      return PropertyCacheStatus::InvalidPropertyId;
    }
    if (found->value_width_bits == 0 || found->value_width_bits > 64) {
      failed_name = requirement.name;
      return PropertyCacheStatus::InvalidValueWidth;
    }
    if (found->value_width_bits < 64 &&
        found->value >= (std::uint64_t{1} << found->value_width_bits)) {
      failed_name = requirement.name;
      return PropertyCacheStatus::ValueOutOfRange;
    }
    *requirement.destination = {found->id, found->value};
  }
  return PropertyCacheStatus::Success;
}

}  // namespace

PropertyCacheResult build_atomic_property_cache(
    const std::span<const ObjectProperty> connector_properties,
    const std::span<const ObjectProperty> crtc_properties,
    const std::span<const ObjectProperty> primary_plane_properties) {
  PropertyCacheResult result;
  const std::array connector_required{
      RequiredProperty{"CRTC_ID", &result.cache.connector.crtc_id}};
  result.status =
      cache_required(connector_properties, connector_required,
                     result.property_name);
  if (result.status != PropertyCacheStatus::Success) return result;

  result.object_type = PropertyObjectType::Crtc;
  const std::array crtc_required{
      RequiredProperty{"MODE_ID", &result.cache.crtc.mode_id},
      RequiredProperty{"ACTIVE", &result.cache.crtc.active}};
  result.status =
      cache_required(crtc_properties, crtc_required, result.property_name);
  if (result.status != PropertyCacheStatus::Success) return result;

  result.object_type = PropertyObjectType::PrimaryPlane;
  const std::array plane_required{
      RequiredProperty{"FB_ID", &result.cache.primary_plane.fb_id},
      RequiredProperty{"CRTC_ID", &result.cache.primary_plane.crtc_id},
      RequiredProperty{"SRC_X", &result.cache.primary_plane.src_x},
      RequiredProperty{"SRC_Y", &result.cache.primary_plane.src_y},
      RequiredProperty{"SRC_W", &result.cache.primary_plane.src_w},
      RequiredProperty{"SRC_H", &result.cache.primary_plane.src_h},
      RequiredProperty{"CRTC_X", &result.cache.primary_plane.crtc_x},
      RequiredProperty{"CRTC_Y", &result.cache.primary_plane.crtc_y},
      RequiredProperty{"CRTC_W", &result.cache.primary_plane.crtc_w},
      RequiredProperty{"CRTC_H", &result.cache.primary_plane.crtc_h}};
  result.status = cache_required(primary_plane_properties, plane_required,
                                 result.property_name);
  return result;
}

}  // namespace glasswyrm::drm
