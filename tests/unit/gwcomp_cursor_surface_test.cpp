#include "backends/output/presentation_backend.hpp"
#include "backends/output/software_frame.hpp"
#include "gwcomp/compositor.hpp"
#include "gwcomp/scene_manifest.hpp"
#include "tests/helpers/test_support.hpp"

#include <glasswyrm/ipc.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fcntl.h>
#include <memory>
#include <span>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace {

using glasswyrm::output::BackendEvent;
using glasswyrm::output::BackendEventKind;
using glasswyrm::output::BackendStateResult;
using glasswyrm::output::PresentDisposition;
using glasswyrm::output::PresentResult;
using glasswyrm::output::PresentationBackend;
using glasswyrm::output::SoftwareFrameView;
using gw::test::require;

class CapturePresenter final : public PresentationBackend {
 public:
  PresentResult present(const SoftwareFrameView& frame) override {
    pixels.assign(frame.pixels.begin(), frame.pixels.end());
    damage.assign(frame.damage.begin(), frame.damage.end());
    return {PresentDisposition::Complete, 0,
            glasswyrm::output::hash_visible_xrgb8888(frame.pixels), {}};
  }
  int poll_fd() const noexcept override { return -1; }
  short poll_events() const noexcept override { return 0; }
  BackendEvent service(short revents) override {
    return revents == 0 ? BackendEvent{}
                        : BackendEvent{BackendEventKind::Fatal, 0, 0,
                                       "unexpected capture event"};
  }
  BackendStateResult suspend(std::string& error) override {
    error.clear();
    return BackendStateResult::Complete;
  }
  PresentResult resume(const SoftwareFrameView& committed) override {
    return present(committed);
  }
  BackendStateResult shutdown(std::string& error) noexcept override {
    error.clear();
    return BackendStateResult::Complete;
  }

  std::vector<std::uint32_t> pixels;
  std::vector<gw::compositor::Rectangle> damage;
};

gwipc_sdr_color_metadata srgb() {
  return {GWIPC_SDR_COLOR_SPACE_SRGB, GWIPC_TRANSFER_FUNCTION_SRGB,
          GWIPC_COLOR_PRIMARIES_SRGB, 0, 0, 0, 0};
}

gwipc_output_upsert output() {
  gwipc_output_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = 1;
  value.enabled = 1;
  value.logical_width = value.physical_pixel_width = 4;
  value.logical_height = value.physical_pixel_height = 4;
  value.refresh_millihertz = 60'000;
  value.scale_numerator = value.scale_denominator = 1;
  value.transform = GWIPC_TRANSFORM_NORMAL;
  value.color = srgb();
  return value;
}

gwipc_surface_upsert normal_surface() {
  gwipc_surface_upsert value{};
  value.struct_size = sizeof(value);
  value.surface_id = 10;
  value.x11_window_id = 100;
  value.output_id = 1;
  value.logical_width = value.logical_height = 4;
  value.stacking = 100;
  value.visible = 1;
  value.transform = GWIPC_TRANSFORM_NORMAL;
  value.opacity = GWIPC_OPACITY_ONE;
  value.scale_numerator = value.scale_denominator = 1;
  value.color = srgb();
  value.fullscreen_eligible = GWIPC_TRI_STATE_FALSE;
  value.direct_scanout_eligible = GWIPC_TRI_STATE_UNKNOWN;
  return value;
}

gwipc_surface_policy_upsert normal_policy() {
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

gwipc_surface_upsert cursor_surface(const std::int32_t x = 1,
                                    const std::int32_t y = 1,
                                    const bool visible = true) {
  gwipc_surface_upsert value{};
  value.struct_size = sizeof(value);
  value.surface_id = 20;
  value.output_id = 1;
  value.logical_x = x;
  value.logical_y = y;
  value.logical_width = value.logical_height = 2;
  value.stacking = -1000;
  value.visible = visible ? 1 : 0;
  value.transform = GWIPC_TRANSFORM_NORMAL;
  value.opacity = GWIPC_OPACITY_ONE;
  value.scale_numerator = value.scale_denominator = 1;
  value.color = srgb();
  value.presentation_flags = GWIPC_SURFACE_PRESENTATION_CURSOR;
  value.fullscreen_eligible = GWIPC_TRI_STATE_UNKNOWN;
  value.direct_scanout_eligible = GWIPC_TRI_STATE_UNKNOWN;
  return value;
}

gwipc_buffer_attach attachment(const std::uint64_t buffer_id,
                               const gwipc_surface_upsert& surface,
                               const gwipc_pixel_format format) {
  gwipc_buffer_attach value{};
  value.struct_size = sizeof(value);
  value.buffer_id = buffer_id;
  value.surface_id = surface.surface_id;
  value.width = surface.logical_width;
  value.height = surface.logical_height;
  value.stride = surface.logical_width * 4U;
  value.storage_size = static_cast<std::uint64_t>(value.stride) * value.height;
  value.pixel_format = format;
  value.alpha_semantics = format == GWIPC_PIXEL_FORMAT_ARGB8888
                              ? GWIPC_ALPHA_PREMULTIPLIED
                              : GWIPC_ALPHA_OPAQUE;
  value.color = srgb();
  value.synchronization = GWIPC_SYNCHRONIZATION_NONE;
  return value;
}

int buffer_fd(const std::span<const std::uint32_t> pixels) {
  const int fd =
      ::memfd_create("gwcomp-cursor-test", MFD_CLOEXEC | MFD_ALLOW_SEALING);
  require(fd >= 0 &&
              ::ftruncate(fd, static_cast<off_t>(pixels.size_bytes())) == 0 &&
              ::pwrite(fd, pixels.data(), pixels.size_bytes(), 0) ==
                  static_cast<ssize_t>(pixels.size_bytes()) &&
              ::fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW) == 0,
          "create sealed cursor test buffer");
  return fd;
}

gwipc_frame_commit frame(const std::uint64_t id) {
  gwipc_frame_commit value{};
  value.struct_size = sizeof(value);
  value.commit_id = id;
  value.output_id = 1;
  value.producer_generation = id;
  return value;
}

void attach_scene(gw::compositor::Compositor& compositor,
                  const gwipc_surface_upsert& cursor, std::string& error,
                  const std::uint64_t normal_buffer = 100,
                  const std::uint64_t cursor_buffer = 200) {
  const std::array<std::uint32_t, 16> blue{
      0xff0000ff, 0xff0000ff, 0xff0000ff, 0xff0000ff,
      0xff0000ff, 0xff0000ff, 0xff0000ff, 0xff0000ff,
      0xff0000ff, 0xff0000ff, 0xff0000ff, 0xff0000ff,
      0xff0000ff, 0xff0000ff, 0xff0000ff, 0xff0000ff};
  const std::array<std::uint32_t, 4> cursor_pixels{
      0xffff0000, 0x80008000, 0x00000000, 0xffffffff};
  const auto normal = normal_surface();
  require(compositor.apply(output()) && compositor.apply(normal) &&
              compositor.apply(normal_policy()) && compositor.apply(cursor),
          "stage normal and cursor surfaces");
  auto normal_attach =
      attachment(normal_buffer, normal, GWIPC_PIXEL_FORMAT_XRGB8888);
  auto cursor_attach =
      attachment(cursor_buffer, cursor, GWIPC_PIXEL_FORMAT_ARGB8888);
  require(compositor.attach(normal_attach, buffer_fd(blue), error) &&
              compositor.attach(cursor_attach, buffer_fd(cursor_pixels), error),
          "attach normal XRGB and cursor ARGB buffers");
}

}  // namespace

int main() {
  constexpr std::uint64_t common =
      GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE |
      GWIPC_CAP_SURFACE_STATE | GWIPC_CAP_SDR_COLOR_METADATA |
      GWIPC_CAP_FRAME_ACKNOWLEDGEMENT | GWIPC_CAP_WINDOW_LIFECYCLE;
  constexpr std::uint64_t buffers =
      GWIPC_CAP_FD_PASSING | GWIPC_CAP_MEMFD_BUFFERS |
      GWIPC_CAP_DAMAGE_REGIONS;
  require(!gw::compositor::select_peer_profile(
              GWIPC_ROLE_PROTOCOL_SERVER,
              common | GWIPC_CAP_CURSOR_SURFACE) &&
              gw::compositor::select_peer_profile(
                  GWIPC_ROLE_PROTOCOL_SERVER,
                  common | buffers | GWIPC_CAP_CURSOR_SURFACE) ==
                  gw::compositor::PeerProfile::M7BufferedProtocolServer,
          "cursor capability requires the complete buffered profile");

  auto capture = std::make_unique<CapturePresenter>();
  auto* observed = capture.get();
  gw::compositor::Compositor compositor(std::move(capture));
  compositor.set_peer_profile(
      gw::compositor::PeerProfile::M7BufferedProtocolServer);
  std::string error;

  require(compositor.begin_snapshot(), "cursor validation snapshot begins");
  auto invalid = cursor_surface();
  invalid.surface_id = 30;
  invalid.logical_width = 65;
  require(!compositor.apply(invalid), "cursor extent is bounded to 64 pixels");
  invalid = cursor_surface();
  invalid.surface_id = 31;
  invalid.x11_window_id = 7;
  require(!compositor.apply(invalid), "cursor has no X11 window identity");
  require(compositor.apply(cursor_surface()), "valid cursor stages");
  auto second = cursor_surface();
  second.surface_id = 21;
  require(!compositor.apply(second), "only one cursor surface exists per output");
  auto cursor_policy = normal_policy();
  cursor_policy.surface_id = 20;
  cursor_policy.x11_window_id = 0;
  require(!compositor.apply(cursor_policy), "cursor rejects GWM policy metadata");
  compositor.abort_snapshot();

  require(compositor.begin_snapshot(), "initial cursor snapshot begins");
  attach_scene(compositor, cursor_surface(), error);
  require(compositor.end_snapshot(), "initial cursor snapshot ends");
  const auto first = compositor.commit(frame(1), error);
  require(first.result == GWIPC_FRAME_ACCEPTED && observed->pixels.size() == 16,
          "cursor frame is accepted by the canonical software path");
  require(observed->pixels[5] == 0xffff0000 &&
              observed->pixels[6] == 0xff00807f &&
              observed->pixels[9] == 0xff0000ff &&
              observed->pixels[10] == 0xffffffff,
          "premultiplied cursor composites above the higher-stacked window");

  require(compositor.begin_snapshot() && compositor.apply(output()) &&
              compositor.apply(normal_surface()) &&
              compositor.apply(normal_policy()) &&
              compositor.apply(cursor_surface(2, 0)) &&
              compositor.end_snapshot(),
          "cursor movement snapshot reuses retained attachments");
  const auto moved = compositor.commit(frame(2), error);
  require(moved.result == GWIPC_FRAME_ACCEPTED &&
              observed->pixels[5] == 0xff0000ff &&
              observed->pixels[2] == 0xffff0000,
          "cursor movement damages old and new bounds without replacing buffer");

  require(compositor.begin_snapshot() && compositor.apply(output()) &&
              compositor.apply(normal_surface()) &&
              compositor.apply(normal_policy()) &&
              compositor.apply(cursor_surface(2, 0, false)) &&
              compositor.end_snapshot(),
          "hidden cursor snapshot stages without policy");
  const auto hidden = compositor.commit(frame(3), error);
  require(hidden.result == GWIPC_FRAME_ACCEPTED &&
              std::ranges::all_of(observed->pixels, [](const auto pixel) {
                return pixel == UINT32_C(0xff0000ff);
              }),
          "cursor visibility change restores the normal surface");

  gw::compositor::Scene manifest_scene;
  manifest_scene.output = output();
  manifest_scene.surfaces.emplace(10, normal_surface());
  manifest_scene.surface_policies.emplace(10, normal_policy());
  manifest_scene.surfaces.emplace(20, cursor_surface(2, 0));
  gw::compositor::SceneManifestResult manifest_result;
  std::string json;
  require(gw::compositor::SceneManifest::describe(
              4, 4, manifest_scene, manifest_result, json, error) &&
              manifest_result.surface_count == 1 &&
              manifest_result.cursor_count == 1 &&
              json.find("\"surface_count\":1") != std::string::npos &&
              json.find("\"cursor_surface\":{\"surface_id\":20") !=
                  std::string::npos &&
              json.find("\"format\":\"ARGB8888Premultiplied\"") !=
                  std::string::npos,
          "scene manifest reports deterministic cursor metadata separately");

  compositor.disconnect();
  require(compositor.begin_snapshot(), "cursor replay snapshot begins");
  attach_scene(compositor, cursor_surface(0, 2), error, 101, 201);
  require(compositor.end_snapshot(), "cursor replay snapshot ends");
  const auto replayed = compositor.commit(frame(1), error);
  require(replayed.result == GWIPC_FRAME_ACCEPTED &&
              observed->pixels[8] == 0xffff0000,
          "complete replay restores the cursor and canonical frame");
}
