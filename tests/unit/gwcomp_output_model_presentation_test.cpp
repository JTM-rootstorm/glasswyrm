#include "gwcomp/compositor.hpp"

#include "tests/helpers/test_support.hpp"

#include <glasswyrm/ipc.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <unistd.h>

namespace {

gwipc_sdr_color_metadata srgb() {
  return {GWIPC_SDR_COLOR_SPACE_SRGB,
          GWIPC_TRANSFER_FUNCTION_SRGB,
          GWIPC_COLOR_PRIMARIES_SRGB,
          0,
          0,
          0,
          0};
}

gwipc_output_upsert enabled_output() {
  gwipc_output_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = 1;
  value.enabled = 1;
  value.logical_width = value.physical_pixel_width = 2;
  value.logical_height = value.physical_pixel_height = 2;
  value.refresh_millihertz = 60'000;
  value.scale_numerator = value.scale_denominator = 1;
  value.transform = GWIPC_TRANSFORM_NORMAL;
  value.color = srgb();
  return value;
}

gwipc_output_upsert disabled_output() {
  auto value = enabled_output();
  value.enabled = 0;
  value.logical_width = value.physical_pixel_width = 0;
  value.logical_height = value.physical_pixel_height = 0;
  value.refresh_millihertz = 0;
  return value;
}

gwipc_surface_upsert metadata_surface() {
  gwipc_surface_upsert value{};
  value.struct_size = sizeof(value);
  value.surface_id = 10;
  value.x11_window_id = 100;
  value.output_id = 1;
  value.logical_width = value.logical_height = 1;
  value.visible = 1;
  value.transform = GWIPC_TRANSFORM_NORMAL;
  value.opacity = GWIPC_OPACITY_ONE;
  value.scale_numerator = value.scale_denominator = 1;
  value.color = srgb();
  value.presentation_flags = GWIPC_SURFACE_PRESENTATION_METADATA_ONLY;
  value.fullscreen_eligible = GWIPC_TRI_STATE_FALSE;
  value.direct_scanout_eligible = GWIPC_TRI_STATE_UNKNOWN;
  return value;
}

gwipc_surface_policy_upsert metadata_policy() {
  gwipc_surface_policy_upsert value{};
  value.struct_size = sizeof(value);
  value.surface_id = 10;
  value.x11_window_id = 100;
  value.workspace_id = 1;
  value.window_type = GWIPC_POLICY_WINDOW_NORMAL;
  value.applied_state = GWIPC_POLICY_APPLIED_NORMAL;
  value.managed = 1;
  value.fullscreen_eligible = GWIPC_TRI_STATE_FALSE;
  value.direct_scanout_eligible = GWIPC_TRI_STATE_UNKNOWN;
  return value;
}

gwipc_frame_commit frame() {
  gwipc_frame_commit value{};
  value.struct_size = sizeof(value);
  value.commit_id = 1;
  value.producer_generation = 1;
  return value;
}

std::filesystem::path temporary_directory(const std::string_view suffix) {
  auto pattern = std::filesystem::temp_directory_path() /
                 ("glasswyrm-output-model-presentation-" + std::string(suffix) +
                  "-XXXXXX");
  auto path = pattern.string();
  gw::test::require(::mkdtemp(path.data()) != nullptr,
                    "create compositor presentation directory");
  return path;
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), {}};
}

void output_model_presents_frame_set() {
  using gw::compositor::PeerProfile;
  using gw::compositor::SceneProfile;
  using Disposition = gw::compositor::PresentedFrame::Disposition;

  const auto root = temporary_directory("enabled");
  const auto manifest = root / "scene.jsonl";
  std::string error;
  {
    gw::compositor::Compositor compositor(root, manifest);
    compositor.set_peer_profile(PeerProfile::M7BufferedProtocolServer);
    gw::test::require(
        compositor.configure_scene_profile(SceneProfile::OutputModel, 1, 7) &&
            compositor.begin_snapshot(7) &&
            compositor.apply(enabled_output()) && compositor.end_snapshot(),
        "complete output-model snapshot stages");

    const auto presented = compositor.commit(frame(), error);
    const auto frame_sets = read_file(root / "frame-sets.jsonl");
    const auto scene = read_file(manifest);
    gw::test::require(
        presented.disposition == Disposition::Complete &&
            presented.result == GWIPC_FRAME_ACCEPTED &&
            presented.ordinal == 1 && presented.hash != 0 &&
            compositor.accepted_frames() == 1 &&
            std::filesystem::exists(
                root / "frame-000001-output-0000000000000001.ppm") &&
            frame_sets.find("\"transaction_ordinal\":1") != std::string::npos &&
            frame_sets.find("\"commit_id\":1") != std::string::npos &&
            frame_sets.find("\"output_count\":1") != std::string::npos &&
            scene.find("\"schema\":\"glasswyrm-scene-v2\"") !=
                std::string::npos &&
            scene.find("\"layout_generation\":7") != std::string::npos,
        "output-model commit renders, presents, and publishes one atomic "
        "frame set: " +
            error);
  }
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}

void historical_disabled_output_stays_dropped() {
  using Disposition = gw::compositor::PresentedFrame::Disposition;

  const auto root = temporary_directory("disabled");
  std::string error;
  {
    gw::compositor::Compositor compositor(root);
    gw::test::require(compositor.begin_snapshot() &&
                          compositor.apply(disabled_output()) &&
                          compositor.end_snapshot(),
                      "historical disabled-output snapshot stages");
    auto commit = frame();
    commit.output_id = 1;
    const auto presented = compositor.commit(commit, error);
    gw::test::require(
        presented.disposition == Disposition::Complete &&
            presented.result == GWIPC_FRAME_DROPPED && presented.ordinal == 0 &&
            presented.hash == 0 && compositor.accepted_frames() == 0 &&
            !std::filesystem::exists(root / "frame-sets.jsonl") &&
            !std::filesystem::exists(root / "frames.jsonl"),
        "historical disabled output promotes without presentation: " + error);
  }
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}

void output_model_metadata_only_stays_manifest_only() {
  using gw::compositor::PeerProfile;
  using gw::compositor::SceneProfile;
  using Disposition = gw::compositor::PresentedFrame::Disposition;

  const auto root = temporary_directory("metadata");
  const auto manifest = root / "scene.jsonl";
  std::string error;
  {
    gw::compositor::Compositor compositor(root, manifest);
    compositor.set_peer_profile(PeerProfile::M6MetadataProtocolServer);
    gw::test::require(
        compositor.configure_scene_profile(SceneProfile::OutputModel, 1, 9) &&
            compositor.begin_snapshot(9) &&
            compositor.apply(enabled_output()) &&
            compositor.apply(metadata_surface()) &&
            compositor.apply(metadata_policy()) && compositor.end_snapshot(),
        "metadata-only output-model snapshot stages");
    const auto presented = compositor.commit(frame(), error);
    const auto scene = read_file(manifest);
    gw::test::require(
        presented.disposition == Disposition::Complete &&
            presented.result == GWIPC_FRAME_ACCEPTED &&
            presented.ordinal == 1 && presented.hash != 0 &&
            compositor.accepted_frames() == 1 &&
            !std::filesystem::exists(root / "frame-sets.jsonl") &&
            scene.find("\"schema\":\"glasswyrm-scene-v2\"") !=
                std::string::npos &&
            scene.find("\"surface_id\":10") != std::string::npos &&
            scene.find("\"metadata_only\":true") != std::string::npos &&
            scene.find("\"memberships\"") == std::string::npos &&
            scene.find("\"surface_count\":1") != std::string::npos,
        "metadata-only output model publishes its v2 scene without a frame "
        "set: " +
            error);
  }
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}

} // namespace

int main() {
  output_model_presents_frame_set();
  historical_disabled_output_stays_dropped();
  output_model_metadata_only_stays_manifest_only();
  return 0;
}
