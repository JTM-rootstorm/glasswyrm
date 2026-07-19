#include "backends/drm/edid_digest.hpp"
#include "backends/drm/inventory.hpp"

#include "output/model/identity.hpp"
#include "output/model/layout.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace {

using glasswyrm::drm::ConnectionStatus;
using glasswyrm::drm::Connector;
using glasswyrm::drm::ConnectorType;
using glasswyrm::drm::DeviceSnapshot;
using glasswyrm::drm::Mode;

DeviceSnapshot snapshot() {
  DeviceSnapshot value;
  value.canonical_path = "/dev/dri/card0";
  value.driver.bus_info = "pci:0000:00:02.0";

  Connector connector;
  connector.id = 71;
  connector.type = static_cast<std::uint32_t>(ConnectorType::DisplayPort);
  connector.type_id = 2;
  connector.status = ConnectionStatus::Connected;
  connector.physical_width_mm = 600;
  connector.physical_height_mm = 340;
  const std::array<std::uint8_t, 8> edid{0x00, 0xff, 0xff, 0xff,
                                         0xff, 0xff, 0xff, 0x00};
  const auto digest = glasswyrm::drm::derive_edid_identity_digest(edid);
  connector.edid_digest.assign(digest.begin(), digest.end());
  connector.modes = {
      Mode{"1920x1080", 1920, 1080, 60'000, 148'500, true},
      Mode{"1280x720", 1280, 720, 60'000, 74'250, false},
  };
  connector.modes[0].flags = 5;
  connector.modes[1].flags = 7;
  value.connectors.push_back(std::move(connector));
  return value;
}

} // namespace

int main() {
  using namespace glasswyrm;
  using gw::test::require;

  const std::array<std::uint8_t, 8> first_edid{0x00, 0xff, 0xff, 0xff,
                                               0xff, 0xff, 0xff, 0x00};
  constexpr drm::EdidIdentityDigest expected_digest{0x1a, 0xfe, 0xf2, 0x69,
                                                    0xd0, 0xaa, 0xe6, 0x03};
  auto second_edid = first_edid;
  second_edid.back() = 0x01;
  require(drm::derive_edid_identity_digest({}) == drm::EdidIdentityDigest{} &&
              drm::derive_edid_identity_digest(first_edid) == expected_digest &&
              drm::derive_edid_identity_digest(first_edid) ==
                  drm::derive_edid_identity_digest(first_edid) &&
              drm::derive_edid_identity_digest(first_edid) !=
                  drm::derive_edid_identity_digest(second_edid),
          "EDID identity digest is absent for no data and stable for bytes");

  std::string error;
  auto discovered = snapshot();
  const auto inventory =
      drm::build_drm_output_inventory(discovered, {0, 1}, error);
  require(inventory && error.empty() && inventory->edid_participated &&
              static_cast<bool>(output::validate_layout(inventory->layout)),
          "selected DRM connector builds a valid stable inventory");

  const auto output_id = inventory->layout.output_order.front();
  const auto &descriptor = inventory->layout.descriptors.at(output_id);
  const auto &state = inventory->layout.states.at(output_id);
  require(
      descriptor.name == "DP-2" && descriptor.kind == output::OutputKind::Drm &&
          descriptor.connected && descriptor.physical_width_mm == 600 &&
          descriptor.physical_height_mm == 340 &&
          descriptor.supported_transform_mask ==
              output::kAllOutputTransformsMask &&
          !descriptor.mode_configurable && descriptor.scale_configurable &&
          descriptor.transform_configurable && descriptor.primary_eligible &&
          !descriptor.arbitrary_headless_mode,
      "DRM descriptor publishes fixed-mode software-transform capabilities");
  require(
      descriptor.modes.size() == 2 && !descriptor.modes[0].current &&
          descriptor.modes[1].current && descriptor.modes[0].preferred &&
          !descriptor.modes[1].preferred && descriptor.modes[0].flags == 5 &&
          descriptor.modes[1].flags == 7,
      "DRM inventory retains every mode and marks only the selection current");
  require(state.enabled && state.primary && state.logical_x == 0 &&
              state.logical_y == 0 && state.logical_width == 1280 &&
              state.logical_height == 720 && state.physical_width == 1280 &&
              state.physical_height == 720 &&
              state.refresh_millihertz == 60'000 &&
              state.scale == output::RationalScale{1, 1} &&
              state.transform == output::OutputTransform::Normal &&
              state.generation == 1 && inventory->layout.generation == 1 &&
              inventory->layout.primary_output_id == output_id &&
              inventory->layout.root_logical_width == 1280 &&
              inventory->layout.root_logical_height == 720,
          "DRM current state is one primary native output at generation one");

  const auto repeated =
      drm::build_drm_output_inventory(discovered, {0, 1}, error);
  require(repeated &&
              repeated->layout.output_order == inventory->layout.output_order &&
              repeated->layout.descriptors.at(output_id).modes[0].id ==
                  descriptor.modes[0].id,
          "unchanged DRM facts reproduce output and mode identities");

  auto without_edid = discovered;
  without_edid.connectors[0].edid_digest.clear();
  const auto no_edid =
      drm::build_drm_output_inventory(without_edid, {0, 1}, error);
  require(no_edid && !no_edid->edid_participated &&
              no_edid->layout.primary_output_id != output_id,
          "optional EDID participation is explicit and identity-relevant");

  auto sysfs_identified = discovered;
  sysfs_identified.driver.bus_info.clear();
  sysfs_identified.sysfs_identity = "sysfs:/devices/platform/vkms/drm/card0";
  const auto sysfs_inventory =
      drm::build_drm_output_inventory(sysfs_identified, {0, 1}, error);
  require(sysfs_inventory &&
              sysfs_inventory->layout.primary_output_id != output_id,
          "canonical sysfs identity is the stable fallback without bus info");

  auto no_bus = discovered;
  no_bus.driver.bus_info.clear();
  no_bus.sysfs_identity.clear();
  require(
      !drm::build_drm_output_inventory(no_bus, {0, 1}, error) &&
          error.find("stable") != std::string::npos,
      "transient DRM object and device paths cannot replace stable identity");

  auto malformed_digest = discovered;
  malformed_digest.connectors[0].edid_digest.pop_back();
  require(!drm::build_drm_output_inventory(malformed_digest, {0, 1}, error) &&
              error.find("digest") != std::string::npos,
          "DRM inventory rejects ambiguous EDID identity material");

  auto disconnected = discovered;
  disconnected.connectors[0].status = ConnectionStatus::Disconnected;
  require(!drm::build_drm_output_inventory(disconnected, {0, 1}, error) &&
              error.find("connected") != std::string::npos,
          "disconnected selected connector is rejected");
  require(!drm::build_drm_output_inventory(discovered, {1, 0}, error) &&
              error.find("connector") != std::string::npos &&
              !drm::build_drm_output_inventory(discovered, {0, 2}, error) &&
              error.find("mode") != std::string::npos,
          "selection indices must address discovered connector and mode facts");

  auto collision = discovered;
  collision.connectors[0].modes.push_back(collision.connectors[0].modes[0]);
  require(!drm::build_drm_output_inventory(collision, {0, 1}, error) &&
              error.find("colliding") != std::string::npos,
          "ambiguous stable mode identity fails instead of using object order");

  auto excessive = discovered;
  excessive.connectors[0].modes[1].width = output::kMaximumPhysicalExtent + 1;
  require(!drm::build_drm_output_inventory(excessive, {0, 1}, error) &&
              error.find("invalid DRM output inventory") != std::string::npos,
          "backend inventory remains inside component-neutral physical bounds");
  return 0;
}
