#include "output/model/identity.hpp"

#include "helpers/test_support.hpp"

#include <array>
#include <cstdint>
#include <string>

int main() {
  using namespace glasswyrm::output;
  using gw::test::require;

  const auto headless = derive_headless_output_id("Glasswyrm-1");
  require(headless && headless->value == UINT64_C(0xa72af207c43e61ea),
          "headless identity has a stable FNV-1a value");
  require((headless->value & kOutputIdentityNamespace) != 0,
          "headless identity carries the output namespace bit");
  require(!derive_headless_output_id("") &&
              !derive_headless_output_id(std::string(64, 'x')),
          "headless identity rejects unbounded or empty names");
  require(derive_headless_output_id("Glasswyrm-2") != headless,
          "headless names produce distinct identities");
  const std::array unique_outputs{*headless,
                                  *derive_headless_output_id("Glasswyrm-2")};
  const std::array collided_outputs{*headless, *headless};
  require(output_identities_are_unique(unique_outputs) &&
              !output_identities_are_unique(collided_outputs),
          "startup identity validation detects stable-ID collisions");

  const std::array<std::uint8_t, 4> digest{0x01, 0x23, 0x45, 0x67};
  const auto drm =
      derive_drm_output_id({"pci:0000:00:02.0", "card0-HDMI-A-1", digest});
  require(drm && drm->id.value == UINT64_C(0xf7188e67e32fb39a) &&
              drm->edid_participated,
          "DRM identity hashes ordered stable facts and records EDID use");
  const auto drm_without_edid =
      derive_drm_output_id({"pci:0000:00:02.0", "card0-HDMI-A-1", {}});
  require(drm_without_edid && !drm_without_edid->edid_participated &&
              drm_without_edid->id != drm->id,
          "EDID participation is explicit and identity-relevant");
  require(!derive_drm_output_id({{}, "card0-HDMI-A-1", {}}) &&
              !derive_drm_output_id({"pci:0000:00:02.0", {}, {}}),
          "DRM identity refuses transient incomplete facts");

  const auto mode =
      derive_output_mode_id(*headless, 800, 600, 60'000, 0, "800x600");
  require(mode && mode->value == UINT64_C(0x4cfc0f6a9f9854c8) &&
              (mode->value & kModeIdentityNamespace) != 0 &&
              (mode->value & kOutputIdentityNamespace) == 0,
          "mode identity has a stable disjoint namespace value");
  require(derive_output_mode_id(*headless, 800, 600, 59'940, 0, "800x600") !=
                  mode &&
              derive_output_mode_id(*headless, 800, 600, 60'000, 1,
                                    "800x600") != mode &&
              derive_output_mode_id(*headless, 800, 600, 60'000, 0,
                                    "800x600i") != mode,
          "mode timing, flags, and name all participate in identity");
  require(!derive_output_mode_id({}, 800, 600, 60'000, 0, "800x600") &&
              !derive_output_mode_id(*headless, 0, 600, 60'000, 0, "800x600") &&
              !derive_output_mode_id(*headless, 800, 600, 0, 0, "800x600"),
          "mode identity rejects incomplete stable facts");
  const std::array unique_modes{
      *mode, *derive_output_mode_id(*headless, 640, 480, 60'000, 0, "640x480")};
  const std::array collided_modes{*mode, *mode};
  require(mode_identities_are_unique(unique_modes) &&
              !mode_identities_are_unique(collided_modes),
          "startup mode validation detects stable-ID collisions");
  return 0;
}
