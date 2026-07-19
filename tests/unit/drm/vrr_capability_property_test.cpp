#include "backends/drm/fake_kms_api.hpp"
#include "backends/drm/property_cache.hpp"
#include "backends/drm/vrr_capability.hpp"
#include "backends/drm/vrr_property.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <string>
#include <vector>

int main() {
  using namespace glasswyrm::drm;

  const std::array<std::uint64_t, 0> absent{};
  const std::array<std::uint64_t, 1> disabled{0};
  const std::array<std::uint64_t, 1> capable{1};
  const std::array<std::uint64_t, 1> invalid{2};
  const std::array<std::uint64_t, 2> duplicate{1, 1};
  gw::test::require(classify_connector_vrr_capability(absent).status ==
                        ConnectorVrrCapabilityStatus::Absent,
                    "absent connector property is not capable");
  gw::test::require(classify_connector_vrr_capability(disabled).status ==
                        ConnectorVrrCapabilityStatus::NotCapable,
                    "zero connector property is present but not capable");
  const auto supported = classify_connector_vrr_capability(capable);
  gw::test::require(supported.status == ConnectorVrrCapabilityStatus::Capable &&
                        supported.property_present && supported.capable,
                    "one connector property is capable");
  gw::test::require(
      classify_connector_vrr_capability(invalid).status ==
              ConnectorVrrCapabilityStatus::Malformed &&
          classify_connector_vrr_capability(duplicate).status ==
              ConnectorVrrCapabilityStatus::Malformed,
      "invalid and duplicate connector capability data are malformed");

  std::vector<ObjectProperty> crtc{{20, "MODE_ID", 9, 64},
                                   {21, "ACTIVE", 1, 1}};
  gw::test::require(discover_vrr_enabled_property(crtc).status ==
                        VrrPropertyStatus::Absent,
                    "ordinary atomic CRTC may omit VRR_ENABLED");
  crtc.push_back({22, "VRR_ENABLED", 0, 1});
  gw::test::require(discover_vrr_enabled_property(crtc).status ==
                        VrrPropertyStatus::InvalidRange,
                    "VRR_ENABLED without boolean range metadata is rejected");
  crtc.back().range = PropertyValueRange{0, 1};
  const auto property = discover_vrr_enabled_property(crtc);
  gw::test::require(property.status == VrrPropertyStatus::Success &&
                        property.binding.id == 22 &&
                        property.binding.value_width_bits == 1 &&
                        property.binding.range->maximum == 1,
                    "VRR_ENABLED ID, value width, and range are retained");
  crtc.back().value = 2;
  gw::test::require(discover_vrr_enabled_property(crtc).status ==
                        VrrPropertyStatus::InvalidValue,
                    "VRR_ENABLED current value must be boolean");

  FakeKmsApi api;
  const PropertyBinding binding{22, 0, 1, PropertyValueRange{0, 1}};
  const std::array<AtomicPropertyValue, 2> selected{
      AtomicPropertyValue{10, 11, 40}, AtomicPropertyValue{40, 20, 90}};
  std::string error;
  gw::test::require(
      !test_vrr_controllability(api, 3, false, 40, binding, selected, error) &&
          error.find("not VRR capable") != std::string::npos,
      "controllability requires a capable connector");
  gw::test::require(!test_vrr_controllability(api, 3, true, 40, std::nullopt,
                                              selected, error) &&
                        error.find("property is absent") != std::string::npos,
                    "controllability requires the optional CRTC property");
  gw::test::require(
      test_vrr_controllability(api, 3, true, 40, binding, selected, error),
      "VRR controllability accepts TEST_ONLY off and on");
  gw::test::require(
      api.atomic_commits.size() == 2 &&
          api.atomic_commits[0].flags ==
              (AtomicTestOnly | AtomicAllowModeset) &&
          api.atomic_commits[0].properties.size() == selected.size() + 1 &&
          api.atomic_commits[0].properties.back().value == 0 &&
          api.atomic_commits[1].properties.back().value == 1,
      "VRR tests preserve selected state and vary only VRR_ENABLED");
  api.rejected_test_property = std::pair{binding.id, std::uint64_t{1}};
  gw::test::require(
      !test_vrr_controllability(api, 3, true, 40, binding, selected, error) &&
          error.find("VRR_ENABLED=1 TEST_ONLY") != std::string::npos,
      "TEST_ONLY on rejection retains the exact failing value");
  return 0;
}
