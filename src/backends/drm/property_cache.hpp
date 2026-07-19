#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace glasswyrm::drm {

struct PropertyValueRange {
  std::uint64_t minimum{};
  std::uint64_t maximum{};
};

struct ObjectProperty {
  std::uint32_t id{};
  std::string name;
  std::uint64_t value{};
  std::uint8_t value_width_bits{64};
  std::optional<PropertyValueRange> range{};
};

struct PropertyBinding {
  std::uint32_t id{};
  std::uint64_t value{};
  std::uint8_t value_width_bits{64};
  std::optional<PropertyValueRange> range{};
};

struct ConnectorPropertyCache {
  PropertyBinding crtc_id;
};

enum class OptionalVrrPropertyStatus {
  Absent,
  Valid,
  Duplicate,
  InvalidId,
  InvalidWidth,
  InvalidRange,
  InvalidValue,
};

struct CrtcPropertyCache {
  PropertyBinding mode_id;
  PropertyBinding active;
  std::optional<PropertyBinding> vrr_enabled;
  OptionalVrrPropertyStatus vrr_status{OptionalVrrPropertyStatus::Absent};
};

struct PlanePropertyCache {
  PropertyBinding fb_id;
  PropertyBinding crtc_id;
  PropertyBinding src_x;
  PropertyBinding src_y;
  PropertyBinding src_w;
  PropertyBinding src_h;
  PropertyBinding crtc_x;
  PropertyBinding crtc_y;
  PropertyBinding crtc_w;
  PropertyBinding crtc_h;
};

struct AtomicPropertyCache {
  ConnectorPropertyCache connector;
  CrtcPropertyCache crtc;
  PlanePropertyCache primary_plane;
};

enum class PropertyObjectType { Connector, Crtc, PrimaryPlane };

enum class PropertyCacheStatus {
  Success,
  MissingProperty,
  DuplicateProperty,
  InvalidPropertyId,
  InvalidValueWidth,
  ValueOutOfRange,
  InvalidPropertyRange,
};

struct PropertyCacheResult {
  PropertyCacheStatus status{PropertyCacheStatus::MissingProperty};
  AtomicPropertyCache cache;
  PropertyObjectType object_type{PropertyObjectType::Connector};
  std::string property_name;
};

[[nodiscard]] PropertyCacheResult build_atomic_property_cache(
    std::span<const ObjectProperty> connector_properties,
    std::span<const ObjectProperty> crtc_properties,
    std::span<const ObjectProperty> primary_plane_properties);

} // namespace glasswyrm::drm
