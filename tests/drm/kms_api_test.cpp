#include "backends/drm/dumb_buffer.hpp"
#include "backends/drm/fake_kms_api.hpp"
#include "backends/drm/kms_state.hpp"
#include "tests/helpers/test_support.hpp"
#include <algorithm>
#include <array>
#include <string>
#include <vector>
namespace {
using namespace glasswyrm::drm;
std::vector<ObjectProperty> props(std::initializer_list<const char *> names,
                                  std::uint32_t first) {
  std::vector<ObjectProperty> out;
  for (auto name : names)
    out.push_back({first++, name, first * 10U, 64});
  return out;
}
void configure(FakeKmsApi &api) {
  api.connector_crtcs[10] = 40;
  KmsMode mode{};
  mode.hdisplay = 1024;
  mode.vdisplay = 768;
  mode.name = "1024x768";
  api.crtcs[40] = {40, 60, 0, 0, true, mode};
  api.planes[50] = {50, 60, 40,           0,          0, 1024, 768,
                    0,  0,  1024U << 16U, 768U << 16U};
  api.properties[{KmsObjectType::Connector, 10}] = props({"CRTC_ID"}, 10);
  api.properties[{KmsObjectType::Crtc, 40}] = props({"MODE_ID", "ACTIVE"}, 20);
  api.properties[{KmsObjectType::Plane, 50}] =
      props({"FB_ID", "CRTC_ID", "SRC_X", "SRC_Y", "SRC_W", "SRC_H", "CRTC_X",
             "CRTC_Y", "CRTC_W", "CRTC_H"},
            30);
}
} // namespace
int main() {
  using namespace glasswyrm::drm;
  std::string error;
  FakeKmsApi api;
  configure(api);
  Mode discovered_mode{"1024x768", 1024, 768, 60'000, 65'000, true};
  discovered_mode.hsync_start = 1048;
  discovered_mode.hsync_end = 1184;
  discovered_mode.htotal = 1344;
  discovered_mode.hskew = 2;
  discovered_mode.vsync_start = 771;
  discovered_mode.vsync_end = 777;
  discovered_mode.vtotal = 806;
  discovered_mode.vscan = 1;
  discovered_mode.flags = 5;
  discovered_mode.type = 9;
  discovered_mode.vrefresh_hz = 60;
  const auto native_mode = kms_mode_from_discovered(discovered_mode);
  gw::test::require(
      native_mode.clock == 65'000 && native_mode.hdisplay == 1024 &&
          native_mode.hsync_start == 1048 && native_mode.hsync_end == 1184 &&
          native_mode.htotal == 1344 && native_mode.hskew == 2 &&
          native_mode.vdisplay == 768 && native_mode.vsync_start == 771 &&
          native_mode.vsync_end == 777 && native_mode.vtotal == 806 &&
          native_mode.vscan == 1 && native_mode.vrefresh == 60 &&
          native_mode.flags == 5 && native_mode.type == 9 &&
          native_mode.name == "1024x768",
      "discovered DRM timings convert without synthetic reconstruction");
  gw::test::require(api.acquire_master(3, error) && api.master &&
                        api.drop_master(3, error) && !api.master,
                    "DRM master ownership operations");

  KmsDumbBufferApi dumb_api(api, 3);
  DumbBuffer buffer;
  gw::test::require(DumbBuffer::create(dumb_api, 2, 2, buffer, error),
                    "KmsApi adapts CREATE_DUMB/AddFB2/MAP_DUMB/mmap");
  buffer.reset();
  const std::array<std::string, 3> cleanup{"rmfb:70", "unmap:24",
                                           "destroy_dumb:7"};
  gw::test::require(
      std::equal(cleanup.begin(), cleanup.end(), api.calls.end() - 3),
      "KMS dumb cleanup order is RMFB/unmap/destroy");
  api.fail_next(KmsOperation::AddFb2);
  gw::test::require(!DumbBuffer::create(dumb_api, 2, 2, buffer, error),
                    "injected AddFB2 failure propagates");

  SavedKmsState saved;
  const PipelineIds ids{10, 40, 50};
  const std::array connectors{10U};
  gw::test::require(
      capture_saved_state(api, 3, ids, connectors, true, saved, error),
      "atomic KMS state captured by value");
  const auto initial =
      atomic_initial_request(ids, saved.properties, 90, 70, 1024, 768);
  const std::array<std::uint64_t, 13> expected_values{
      40, 90, 1, 70, 40, 0, 0, 1024ULL << 16U, 768ULL << 16U, 0, 0, 1024, 768};
  const std::array<std::uint32_t, 13> expected_objects{
      10, 40, 40, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50};
  const std::array<std::uint32_t, 13> expected_properties{
      10, 20, 21, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39};
  bool initial_is_exact = initial.size() == expected_values.size();
  for (std::size_t i = 0; initial_is_exact && i < initial.size(); ++i)
    initial_is_exact = initial[i].object_id == expected_objects[i] &&
                       initial[i].property_id == expected_properties[i] &&
                       initial[i].value == expected_values[i];
  gw::test::require(initial_is_exact,
                    "exact initial atomic property set and 16.16 source size");
  gw::test::require(api.atomic_commit(3, initial,
                                      AtomicTestOnly | AtomicAllowModeset,
                                      nullptr, error) &&
                        api.atomic_commits.back().flags ==
                            (AtomicTestOnly | AtomicAllowModeset),
                    "atomic TEST_ONLY request recorded exactly");
  gw::test::require(
      api.atomic_commit(3, initial, AtomicAllowModeset, nullptr, error) &&
          api.atomic_commits.back().flags == AtomicAllowModeset,
      "initial atomic modeset is blocking after TEST_ONLY");
  PageFlipCookie cookie(99);
  const auto flip = atomic_flip_request(ids, saved.properties, 71);
  gw::test::require(flip.size() == 1 &&
                        api.atomic_commit(3, flip,
                                          AtomicNonblock | AtomicPageFlipEvent,
                                          &cookie, error) &&
                        api.atomic_commits.back().cookie == &cookie,
                    "nonblocking atomic page flip retains stable cookie");
  api.fail_next(KmsOperation::AtomicCommit);
  gw::test::require(!api.atomic_commit(3, flip, AtomicNonblock, &cookie, error),
                    "atomic submission failure injected");
  gw::test::require(api.legacy_page_flip(3, 40, 71, cookie, error) &&
                        api.calls.back() == "pageflip:40:71:99",
                    "legacy PageFlip records exact IDs and cookie");

  auto missing = api; // copy an independent deterministic state
  std::erase_if(missing.properties[{KmsObjectType::Plane, 50}],
                [](const ObjectProperty &p) { return p.name == "SRC_W"; });
  SavedKmsState rejected;
  gw::test::require(!capture_saved_state(missing, 3, ids, connectors, true,
                                         rejected, error) &&
                        error.find("SRC_W") != std::string::npos,
                    "missing atomic property rejected by name");
  const std::array cloned{10U, 11U};
  gw::test::require(
      !capture_saved_state(api, 3, ids, cloned, true, rejected, error),
      "cloned saved state rejected");

  api.connector_crtcs[10] = 0;
  api.crtcs[40] = {40, 0, 0, 0, false, {}};
  api.planes[50] = {50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  api.atomic_result_connector_crtc = saved.connector_crtc_id;
  api.atomic_result_crtc = saved.crtc;
  api.atomic_result_plane = saved.primary_plane;
  gw::test::require(restore_saved_state(api, 3, saved, error),
                    "atomic saved state restores and verifies");
  gw::test::require(
      api.atomic_commits.back().flags == AtomicAllowModeset &&
          api.calls[api.calls.size() - 1].rfind("destroy_blob:", 0) == 0,
      "restore uses ALLOW_MODESET and destroys owned blob after verification");
  api.atomic_result_crtc = saved.crtc;
  api.atomic_result_crtc->framebuffer_id = 999;
  gw::test::require(!restore_saved_state(api, 3, saved, error) &&
                        error.find("does not match") != std::string::npos,
                    "restore readback mismatch is fatal");
  api.atomic_result_crtc = saved.crtc;
  api.atomic_result_crtc->mode.clock++;
  gw::test::require(!restore_saved_state(api, 3, saved, error) &&
                        error.find("does not match") != std::string::npos,
                    "restore mode readback mismatch is fatal");

  FakeKmsApi legacy;
  configure(legacy);
  SavedKmsState legacy_saved;
  gw::test::require(capture_saved_state(legacy, 3, ids, connectors, false,
                                        legacy_saved, error),
                    "legacy state captured");
  legacy.crtcs[40] = {40, 0, 0, 0, false, {}};
  legacy.connector_crtcs[10] = 0;
  gw::test::require(restore_saved_state(legacy, 3, legacy_saved, error) &&
                        legacy.legacy_modesets.back().framebuffer_id == 60,
                    "legacy SetCrtc restore and readback verification");
  return 0;
}
