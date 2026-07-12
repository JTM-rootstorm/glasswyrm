#include "gwcomp/compositor.hpp"
#include "tests/helpers/test_support.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

namespace {
void require(bool value, const char *message) {
  gw::test::require(value, message);
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
gwipc_output_upsert output() {
  gwipc_output_upsert v{};
  v.struct_size = sizeof(v);
  v.output_id = 1;
  v.enabled = 1;
  v.logical_width = v.physical_pixel_width = 1024;
  v.logical_height = v.physical_pixel_height = 768;
  v.refresh_millihertz = 60000;
  v.scale_numerator = v.scale_denominator = 1;
  v.transform = GWIPC_TRANSFORM_NORMAL;
  v.color = srgb();
  return v;
}
gwipc_surface_upsert surface(std::uint32_t xid, std::int32_t stack) {
  gwipc_surface_upsert v{};
  v.struct_size = sizeof(v);
  v.surface_id = (UINT64_C(1) << 32) | xid;
  v.x11_window_id = xid;
  v.output_id = 1;
  v.logical_x = static_cast<std::int32_t>(xid - 1000U) * 32;
  v.logical_y = v.logical_x;
  v.logical_width = 320;
  v.logical_height = 200;
  v.stacking = stack;
  v.visible = 1;
  v.transform = GWIPC_TRANSFORM_NORMAL;
  v.opacity = GWIPC_OPACITY_ONE;
  v.scale_numerator = v.scale_denominator = 1;
  v.color = srgb();
  v.presentation_flags = GWIPC_SURFACE_PRESENTATION_METADATA_ONLY;
  v.fullscreen_eligible = GWIPC_TRI_STATE_FALSE;
  v.direct_scanout_eligible = GWIPC_TRI_STATE_UNKNOWN;
  return v;
}
gwipc_surface_policy_upsert policy(std::uint32_t xid) {
  gwipc_surface_policy_upsert v{};
  v.struct_size = sizeof(v);
  v.surface_id = (UINT64_C(1) << 32) | xid;
  v.x11_window_id = xid;
  v.workspace_id = 1;
  v.window_type = GWIPC_POLICY_WINDOW_NORMAL;
  v.applied_state = GWIPC_POLICY_APPLIED_NORMAL;
  v.focused = xid == 1002;
  v.managed = 1;
  v.decoration_eligible = 1;
  v.fullscreen_eligible = GWIPC_TRI_STATE_FALSE;
  v.direct_scanout_eligible = GWIPC_TRI_STATE_UNKNOWN;
  return v;
}
gwipc_frame_commit frame(std::uint64_t id) {
  gwipc_frame_commit v{};
  v.struct_size = sizeof(v);
  v.commit_id = id;
  v.output_id = 1;
  v.producer_generation = id;
  return v;
}
std::string read(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), {}};
}
} // namespace

int main() {
  char temporary[] = "/tmp/gwcomp-metadata-unit-XXXXXX";
  require(::mkdtemp(temporary) != nullptr, "create metadata test directory");
  const std::filesystem::path root = temporary;
  const auto dumps = root / "dumps";
  const auto manifest = root / "scene" / "scenes.jsonl";
  gw::compositor::Compositor compositor(dumps, manifest);
  require(compositor.begin_snapshot(), "metadata snapshot begins");
  require(compositor.apply(output()), "metadata output stages");
  require(compositor.apply(surface(1002, 1)), "second metadata surface stages");
  require(compositor.apply(policy(1002)), "second policy stages");
  require(compositor.apply(surface(1001, 0)), "first metadata surface stages");
  require(compositor.apply(policy(1001)), "first policy stages");
  require(!compositor.apply(policy(1001)),
          "duplicate snapshot policy is rejected");
  require(compositor.end_snapshot(), "metadata snapshot ends");
  std::string error;
  const auto duplicate = compositor.commit(frame(1), error, true);
  require(duplicate.result == GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA,
          "duplicate snapshot policy rejects the complete commit");
  require(!std::filesystem::exists(manifest),
          "duplicate snapshot does not append manifest");
  require(compositor.begin_snapshot(), "clean metadata snapshot begins");
  require(compositor.apply(output()), "clean metadata output stages");
  require(compositor.apply(surface(1002, 1)), "clean second surface stages");
  require(compositor.apply(policy(1002)), "clean second policy stages");
  require(compositor.apply(surface(1001, 0)), "clean first surface stages");
  require(compositor.apply(policy(1001)), "clean first policy stages");
  require(compositor.end_snapshot(), "clean metadata snapshot ends");
  const auto accepted = compositor.commit(frame(2), error, true);
  require(accepted.result == GWIPC_FRAME_ACCEPTED && accepted.hash != 0,
          "metadata-only frame is accepted and hashed");
  const auto first = read(manifest);
  require(first.find("\"surface_count\":2") != std::string::npos &&
              first.find("\"x11_window_id\":1001") <
                  first.find("\"x11_window_id\":1002"),
          "manifest has deterministic visible stack order");
  require(!std::filesystem::exists(dumps),
          "metadata-only commit creates no raster dump directory");
  require(compositor.begin_snapshot(), "replacement metadata snapshot begins");
  require(compositor.apply(output()), "replacement output stages");
  require(compositor.apply(surface(1001, 0)), "unpaired surface stages");
  require(compositor.end_snapshot(), "incomplete replacement snapshot ends");
  const auto rejected = compositor.commit(frame(3), error, true);
  require(rejected.result == GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA,
          "missing policy rejects commit");
  require(read(manifest) == first, "rejected commit does not append manifest");
  compositor.disconnect();
  require(compositor.begin_snapshot(), "buffer rejection snapshot begins");
  require(compositor.apply(output()), "buffer rejection output stages");
  require(compositor.apply(surface(1001, 0)),
          "buffer rejection surface stages");
  gwipc_buffer_attach attachment{};
  attachment.struct_size = sizeof(attachment);
  attachment.buffer_id = 1;
  attachment.surface_id = (UINT64_C(1) << 32) | 1001U;
  require(!compositor.attach(attachment, -1, error) &&
              error.find("metadata-only") != std::string::npos,
          "metadata-only surface rejects buffer attachment");
  const auto blocked_manifest = root / "blocked-manifest";
  std::filesystem::create_directory(blocked_manifest);
  gw::compositor::Compositor failing(root / "unused-dumps", blocked_manifest);
  require(failing.begin_snapshot(), "manifest failure snapshot begins");
  require(failing.apply(output()), "manifest failure output stages");
  require(failing.apply(surface(1001, 0)), "manifest failure surface stages");
  require(failing.apply(policy(1001)), "manifest failure policy stages");
  require(failing.end_snapshot(), "manifest failure snapshot ends");
  const auto failed = failing.commit(frame(1), error, true);
  require(failed.result == GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA,
          "manifest write failure rejects metadata frame");
  std::filesystem::remove(blocked_manifest);
  const auto retry = failing.commit(frame(2), error, true);
  require(retry.result == GWIPC_FRAME_ACCEPTED &&
              std::filesystem::is_regular_file(blocked_manifest),
          "manifest failure preserves pending scene for a clean retry");
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return 0;
}
