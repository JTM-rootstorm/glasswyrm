#include "backends/drm/fake_kms_api.hpp"
#include "backends/drm/kms_state.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <string>
#include <vector>

namespace {
using namespace glasswyrm::drm;

std::vector<ObjectProperty>
properties(const std::initializer_list<const char *> names, std::uint32_t id) {
  std::vector<ObjectProperty> result;
  for (const auto *name : names)
    result.push_back({id++, name, 0, 64});
  return result;
}

void configure(FakeKmsApi &api) {
  api.connector_crtcs[10] = 40;
  KmsMode mode{};
  mode.hdisplay = 1920;
  mode.vdisplay = 1080;
  mode.name = "1920x1080";
  api.crtcs[40] = {40, 60, 0, 0, true, mode};
  api.planes[50] = {50, 60, 40,           0,           0, 1920, 1080,
                    0,  0,  1920U << 16U, 1080U << 16U};
  api.properties[{KmsObjectType::Connector, 10}] = properties({"CRTC_ID"}, 10);
  auto crtc = properties({"MODE_ID", "ACTIVE"}, 20);
  crtc.push_back({22, "VRR_ENABLED", 1, 1, PropertyValueRange{0, 1}});
  api.properties[{KmsObjectType::Crtc, 40}] = std::move(crtc);
  api.properties[{KmsObjectType::Plane, 50}] =
      properties({"FB_ID", "CRTC_ID", "SRC_X", "SRC_Y", "SRC_W", "SRC_H",
                  "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H"},
                 30);
}
} // namespace

int main() {
  using namespace glasswyrm::drm;
  FakeKmsApi api;
  configure(api);
  SavedKmsState saved;
  std::string error;
  const PipelineIds pipeline{10, 40, 50};
  const std::array connectors{10U};
  gw::test::require(
      capture_saved_state(api, 3, pipeline, connectors, true, saved, error) &&
          saved.properties.crtc.vrr_enabled &&
          saved.properties.crtc.vrr_enabled->id == 22 &&
          saved.properties.crtc.vrr_enabled->value == 1,
      "saved KMS state captures original VRR property exactly");
  const auto initial =
      atomic_initial_request(pipeline, saved.properties, 90, 70, 1920, 1080);
  gw::test::require(initial.back().object_id == 40 &&
                        initial.back().property_id == 22 &&
                        initial.back().value == 0,
                    "first Glasswyrm atomic modeset explicitly leaves VRR off");

  auto &live = api.properties[{KmsObjectType::Crtc, 40}];
  live.back().value = 0;
  gw::test::require(restore_saved_state(api, 3, saved, error) &&
                        live.back().value == 1 &&
                        api.atomic_commits.back().properties.back().value == 1,
                    "atomic restore writes and reads original VRR state");
  live.back().value = 0;
  api.property_readback_overrides[{40, 22}] = 0;
  gw::test::require(!restore_saved_state(api, 3, saved, error) &&
                        error.find("VRR_ENABLED state does not match") !=
                            std::string::npos,
                    "VRR readback mismatch is fatal");
  return 0;
}
