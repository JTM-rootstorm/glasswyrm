#include "backends/drm/property_cache.hpp"

#include "tests/helpers/test_support.hpp"

#include <algorithm>
#include <string_view>
#include <vector>

namespace {

std::vector<glasswyrm::drm::ObjectProperty> properties(
    const std::initializer_list<std::string_view> names,
    const std::uint32_t first_id) {
  std::vector<glasswyrm::drm::ObjectProperty> result;
  std::uint32_t id = first_id;
  for (const auto name : names)
    result.push_back({id++, std::string(name), id * 10U, 32});
  return result;
}

}  // namespace

int main() {
  using namespace glasswyrm::drm;
  auto connector = properties({"CRTC_ID", "non-desktop"}, 10);
  auto crtc = properties({"MODE_ID", "ACTIVE", "VRR_ENABLED"}, 20);
  auto plane = properties({"FB_ID", "CRTC_ID", "SRC_X", "SRC_Y", "SRC_W",
                           "SRC_H", "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H",
                           "rotation"},
                          30);
  const auto complete = build_atomic_property_cache(connector, crtc, plane);
  gw::test::require(complete.status == PropertyCacheStatus::Success,
                    "complete atomic property cache accepted");
  gw::test::require(complete.cache.connector.crtc_id.id == 10 &&
                        complete.cache.crtc.active.id == 21 &&
                        complete.cache.primary_plane.crtc_h.id == 39,
                    "typed property IDs cached by discovered name");

  auto missing = plane;
  std::erase_if(missing, [](const ObjectProperty& property) {
    return property.name == "SRC_W";
  });
  const auto missing_result =
      build_atomic_property_cache(connector, crtc, missing);
  gw::test::require(
      missing_result.status == PropertyCacheStatus::MissingProperty &&
          missing_result.object_type == PropertyObjectType::PrimaryPlane &&
          missing_result.property_name == "SRC_W",
      "missing required plane property identified");

  auto duplicate = connector;
  duplicate.push_back(connector.front());
  gw::test::require(build_atomic_property_cache(duplicate, crtc, plane).status ==
                        PropertyCacheStatus::DuplicateProperty,
                    "duplicate required property rejected");

  auto zero_id = crtc;
  zero_id.front().id = 0;
  gw::test::require(build_atomic_property_cache(connector, zero_id, plane)
                            .status == PropertyCacheStatus::InvalidPropertyId,
                    "zero property ID rejected");

  auto invalid_width = plane;
  invalid_width.front().value_width_bits = 0;
  gw::test::require(
      build_atomic_property_cache(connector, crtc, invalid_width).status ==
          PropertyCacheStatus::InvalidValueWidth,
      "zero-width property value rejected");
  invalid_width.front().value_width_bits = 65;
  gw::test::require(
      build_atomic_property_cache(connector, crtc, invalid_width).status ==
          PropertyCacheStatus::InvalidValueWidth,
      "oversized property value width rejected");

  auto out_of_range = plane;
  out_of_range.front().value_width_bits = 8;
  out_of_range.front().value = 256;
  gw::test::require(
      build_atomic_property_cache(connector, crtc, out_of_range).status ==
          PropertyCacheStatus::ValueOutOfRange,
      "property value width checked");

  auto fixed_point = plane;
  const auto src_w = std::find_if(fixed_point.begin(), fixed_point.end(),
                                  [](const ObjectProperty& property) {
                                    return property.name == "SRC_W";
                                  });
  src_w->value = std::uint64_t{4096} << 16U;
  src_w->value_width_bits = 64;
  gw::test::require(build_atomic_property_cache(connector, crtc, fixed_point)
                            .status == PropertyCacheStatus::Success,
                    "16.16 source coordinate value retained");
  return 0;
}
