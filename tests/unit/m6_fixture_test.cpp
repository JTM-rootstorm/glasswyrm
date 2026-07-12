#include "gwcomp/scene_manifest.hpp"
#include "protocol/x11/event.hpp"

#include <glasswyrm/ipc.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>

namespace {
namespace x11 = gw::protocol::x11;
std::string read(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), {}};
}
std::string hex(const std::vector<std::uint8_t> &bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  for (auto byte : bytes) {
    result.push_back(digits[byte >> 4]);
    result.push_back(digits[byte & 15]);
  }
  return result;
}
bool contains(const std::string &json, const std::string &key,
              const std::vector<std::uint8_t> &bytes) {
  return json.find("\"" + key + "\": \"" + hex(bytes) + "\"") !=
         std::string::npos;
}
gwipc_sdr_color_metadata srgb() {
  return {GWIPC_SDR_COLOR_SPACE_SRGB,
          GWIPC_TRANSFER_FUNCTION_SRGB,
          GWIPC_COLOR_PRIMARIES_SRGB,
          0,
          0,
          0,
          0};
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  const std::filesystem::path root = argv[1];
  const auto events = read(root / "structural-events.json");
  const x11::DestroyNotifyEvent destroy{0x01020304, 0xa0b0c0d0};
  const x11::UnmapNotifyEvent unmap{0x01020304, 0xa0b0c0d0, true};
  const x11::MapNotifyEvent map{0x01020304, 0xa0b0c0d0, true};
  const x11::ConfigureNotifyEvent configure{
      0x01020304, 0xa0b0c0d0, 0x11223344, -2, 3, 640, 480, 5, true};
  if (!contains(events, "destroy_little",
                x11::encode_destroy_notify(x11::ByteOrder::LittleEndian, 0x2345,
                                           destroy)) ||
      !contains(events, "destroy_big",
                x11::encode_destroy_notify(x11::ByteOrder::BigEndian, 0x2345,
                                           destroy)) ||
      !contains(events, "unmap_little",
                x11::encode_unmap_notify(x11::ByteOrder::LittleEndian, 0x2345,
                                         unmap)) ||
      !contains(
          events, "unmap_big",
          x11::encode_unmap_notify(x11::ByteOrder::BigEndian, 0x2345, unmap)) ||
      !contains(
          events, "map_little",
          x11::encode_map_notify(x11::ByteOrder::LittleEndian, 0x2345, map)) ||
      !contains(
          events, "map_big",
          x11::encode_map_notify(x11::ByteOrder::BigEndian, 0x2345, map)) ||
      !contains(events, "configure_little",
                x11::encode_configure_notify(x11::ByteOrder::LittleEndian,
                                             0x2345, configure)) ||
      !contains(events, "configure_big",
                x11::encode_configure_notify(x11::ByteOrder::BigEndian, 0x2345,
                                             configure)))
    return 1;

  gw::compositor::Scene scene;
  gwipc_output_upsert output{};
  output.struct_size = sizeof(output);
  output.output_id = 1;
  output.enabled = 1;
  output.logical_width = output.physical_pixel_width = 1024;
  output.logical_height = output.physical_pixel_height = 768;
  output.refresh_millihertz = 60000;
  output.scale_numerator = output.scale_denominator = 1;
  output.transform = GWIPC_TRANSFORM_NORMAL;
  output.color = srgb();
  scene.output = output;
  for (std::uint32_t xid : {1001U, 1002U}) {
    gwipc_surface_upsert surface{};
    surface.struct_size = sizeof(surface);
    surface.surface_id = (UINT64_C(1) << 32) | xid;
    surface.x11_window_id = xid;
    surface.output_id = 1;
    surface.logical_x = surface.logical_y =
        static_cast<std::int32_t>(xid - 1000) * 32;
    surface.logical_width = 320;
    surface.logical_height = 200;
    surface.stacking = xid - 1001;
    surface.visible = 1;
    surface.transform = GWIPC_TRANSFORM_NORMAL;
    surface.opacity = GWIPC_OPACITY_ONE;
    surface.scale_numerator = surface.scale_denominator = 1;
    surface.color = srgb();
    surface.presentation_flags = GWIPC_SURFACE_PRESENTATION_METADATA_ONLY;
    surface.fullscreen_eligible = GWIPC_TRI_STATE_FALSE;
    surface.direct_scanout_eligible = GWIPC_TRI_STATE_UNKNOWN;
    scene.surfaces.emplace(surface.surface_id, surface);
    gwipc_surface_policy_upsert policy{};
    policy.struct_size = sizeof(policy);
    policy.surface_id = surface.surface_id;
    policy.x11_window_id = xid;
    policy.workspace_id = 1;
    policy.window_type = GWIPC_POLICY_WINDOW_NORMAL;
    policy.applied_state = GWIPC_POLICY_APPLIED_NORMAL;
    policy.focused = xid == 1002;
    policy.managed = policy.decoration_eligible = 1;
    policy.fullscreen_eligible = GWIPC_TRI_STATE_FALSE;
    policy.direct_scanout_eligible = GWIPC_TRI_STATE_UNKNOWN;
    scene.surface_policies.emplace(policy.surface_id, policy);
  }
  gw::compositor::SceneManifestResult result;
  std::string actual, error;
  if (!gw::compositor::SceneManifest::describe(2, 2, scene, result, actual,
                                               error))
    return 1;
  const auto expected = read(root / "scene-manifest.jsonl");
  if (actual != expected) {
    std::cerr << actual;
    return 1;
  }
  return 0;
}
