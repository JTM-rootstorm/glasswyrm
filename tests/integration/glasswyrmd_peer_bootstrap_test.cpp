#include "glasswyrmd/compositor_peer.hpp"
#include "glasswyrmd/cursor_presenter.hpp"
#include "glasswyrmd/pixel_storage.hpp"
#include "glasswyrmd/published_buffer.hpp"
#include "glasswyrmd/policy_peer.hpp"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <poll.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
void require(bool value, const char *message) {
  if (!value) {
    std::fprintf(stderr, "peer bootstrap test: %s\n", message);
    std::exit(1);
  }
}
pid_t launch(const char *executable, const std::string &socket,
             const std::string &extra_name = {},
             const std::string &extra = {}) {
  const auto child = ::fork();
  require(child >= 0, "fork peer process");
  if (child == 0) {
    if (extra_name.empty())
      ::execl(executable, executable, "--ipc-socket", socket.c_str(), nullptr);
    else
      ::execl(executable, executable, "--ipc-socket", socket.c_str(),
              extra_name.c_str(), extra.c_str(), nullptr);
    _exit(127);
  }
  return child;
}
template <class Peer> void drive(Peer &peer) {
  std::string error;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (peer.state() != glasswyrm::server::PeerBootstrapState::Synchronized) {
    require(std::chrono::steady_clock::now() < deadline, "bootstrap timed out");
    pollfd descriptor{peer.fd(), peer.wanted_events(), 0};
    require(::poll(&descriptor, 1, 50) >= 0, "poll peer");
    require(peer.process(descriptor.revents, error) ==
                glasswyrm::server::PeerProcessOutcome::Progress,
            error.c_str());
  }
}
template <class Peer> void synchronize(Peer &peer) {
  std::string error;
  while (!peer.connect(error))
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  drive(peer);
}
gwipc_policy_lifecycle_window_upsert
lifecycle(std::uint64_t serial, bool mapped, std::int32_t x, std::int32_t y) {
  gwipc_policy_lifecycle_window_upsert value{};
  value.struct_size = sizeof(value);
  value.window.struct_size = sizeof(value.window);
  value.window.window_id = 100;
  value.window.parent_window_id = 1;
  value.window.workspace_id = 1;
  value.window.requested_x = x;
  value.window.requested_y = y;
  value.window.requested_width = 320;
  value.window.requested_height = 200;
  value.window.window_type = GWIPC_POLICY_WINDOW_NORMAL;
  value.window.map_intent =
      mapped ? GWIPC_POLICY_WANTS_MAP : GWIPC_POLICY_UNMAPPED;
  value.window.creation_serial = 1;
  value.window.map_serial = mapped ? serial : 0;
  value.geometry_serial = serial;
  return value;
}
glasswyrm::server::CompositorSnapshotSubmission
project(std::uint64_t id,
        const glasswyrm::server::PolicySnapshotResult &result) {
  glasswyrm::server::CompositorSnapshotSubmission submission{
      id, id, {}, {}, {}, {}};
  for (const auto &state : result.windows) {
    gwipc_surface_upsert surface{};
    surface.struct_size = sizeof(surface);
    surface.surface_id = state.window_id;
    surface.x11_window_id = state.window_id;
    surface.output_id = 1;
    surface.logical_x = state.final_x;
    surface.logical_y = state.final_y;
    surface.logical_width = state.final_width;
    surface.logical_height = state.final_height;
    surface.stacking = state.stacking;
    surface.visible = state.visible;
    surface.transform = GWIPC_TRANSFORM_NORMAL;
    surface.opacity = GWIPC_OPACITY_ONE;
    surface.scale_numerator = 1;
    surface.scale_denominator = 1;
    surface.color = {GWIPC_SDR_COLOR_SPACE_SRGB,
                     GWIPC_TRANSFER_FUNCTION_SRGB,
                     GWIPC_COLOR_PRIMARIES_SRGB,
                     0,
                     0,
                     0,
                     0};
    surface.presentation_flags = GWIPC_SURFACE_PRESENTATION_METADATA_ONLY;
    surface.fullscreen_eligible = state.fullscreen_eligible;
    surface.direct_scanout_eligible = state.direct_scanout_eligible;
    submission.surfaces.push_back(surface);
    gwipc_surface_policy_upsert policy{};
    policy.struct_size = sizeof(policy);
    policy.surface_id = state.window_id;
    policy.x11_window_id = state.window_id;
    policy.workspace_id = state.workspace_id;
    policy.window_type = state.window_type;
    policy.applied_state = state.applied_state;
    policy.focused = state.focused;
    policy.managed = state.managed;
    policy.decoration_eligible = state.decoration_eligible;
    policy.override_redirect = state.override_redirect;
    policy.attention_requested = state.attention_requested;
    policy.fullscreen_eligible = state.fullscreen_eligible;
    policy.direct_scanout_eligible = state.direct_scanout_eligible;
    submission.policies.push_back(policy);
  }
  return submission;
}
glasswyrm::server::CompositorSnapshotSubmission buffered_project(
    std::uint64_t id,
    const glasswyrm::server::PublishedWindowBuffer* buffer) {
  glasswyrm::server::CompositorSnapshotSubmission submission{
      id, id, {}, {}, {}, {}};
  gwipc_surface_upsert surface{};
  surface.struct_size = sizeof(surface);
  surface.surface_id = (UINT64_C(1) << 32U) | 100U;
  surface.x11_window_id = 100;
  surface.output_id = 1;
  surface.logical_width = 32;
  surface.logical_height = 24;
  surface.visible = 1;
  surface.transform = GWIPC_TRANSFORM_NORMAL;
  surface.opacity = GWIPC_OPACITY_ONE;
  surface.scale_numerator = surface.scale_denominator = 1;
  surface.color = {GWIPC_SDR_COLOR_SPACE_SRGB,
                   GWIPC_TRANSFER_FUNCTION_SRGB,
                   GWIPC_COLOR_PRIMARIES_SRGB,
                   0, 0, 0, 0};
  submission.surfaces.push_back(surface);
  gwipc_surface_policy_upsert policy{};
  policy.struct_size = sizeof(policy);
  policy.surface_id = surface.surface_id;
  policy.x11_window_id = 100;
  policy.workspace_id = 1;
  policy.window_type = GWIPC_POLICY_WINDOW_NORMAL;
  policy.applied_state = GWIPC_POLICY_APPLIED_NORMAL;
  policy.managed = 1;
  policy.decoration_eligible = 1;
  policy.fullscreen_eligible = GWIPC_TRI_STATE_FALSE;
  policy.direct_scanout_eligible = GWIPC_TRI_STATE_UNKNOWN;
  submission.policies.push_back(policy);
  if (buffer) {
    glasswyrm::server::CompositorSnapshotSubmission::Buffer attachment;
    attachment.attach.struct_size = sizeof(attachment.attach);
    attachment.attach.buffer_id = buffer->buffer_id();
    attachment.attach.surface_id = surface.surface_id;
    attachment.attach.width = buffer->width();
    attachment.attach.height = buffer->height();
    attachment.attach.stride = buffer->stride();
    attachment.attach.storage_size = buffer->size();
    attachment.attach.pixel_format = GWIPC_PIXEL_FORMAT_XRGB8888;
    attachment.attach.alpha_semantics = GWIPC_ALPHA_OPAQUE;
    attachment.attach.color = surface.color;
    attachment.attach.synchronization = GWIPC_SYNCHRONIZATION_NONE;
    attachment.fd = buffer->fd();
    submission.buffers.push_back(attachment);
  }
  return submission;
}
void stop(pid_t child) {
  (void)::kill(child, SIGTERM);
  int status = 0;
  require(::waitpid(child, &status, 0) == child, "reap peer process");
}
} // namespace

int main(int argc, char **argv) {
  require(argc == 3, "expected gwm and gwcomp paths");
  char temporary[] = "/tmp/glasswyrmd-peer-bootstrap-XXXXXX";
  require(::mkdtemp(temporary) != nullptr, "create temporary directory");
  const std::string root = temporary;
  const std::string wm_socket = root + "/gwm.sock";
  const std::string comp_socket = root + "/gwcomp.sock";
  const auto wm = launch(argv[1], wm_socket);
  glasswyrm::server::PolicyPeer policy(wm_socket,
                                       gw::protocol::x11::kScreenModel);
  synchronize(policy);
  require(policy.policy_hash() != 0, "policy bootstrap reports canonical hash");
  std::vector<glasswyrm::server::PolicySnapshotResult> results;
  std::string error;
  for (std::uint64_t id = 2; id <= 5; ++id) {
    const auto window =
        lifecycle(id, id != 2 && id != 5, id >= 4 ? 44 : 10, id >= 4 ? 55 : 20);
    require(policy.submit({id, id, {window}}, error), error.c_str());
    drive(policy);
    results.push_back(policy.result());
  }
  require(policy.submit({6, 6, {}}, error), error.c_str());
  drive(policy);
  results.push_back(policy.result());
  stop(wm);
  const std::string legacy_wm_socket = root + "/gwm-legacy.sock";
  const auto legacy_wm = launch(argv[1], legacy_wm_socket);
  glasswyrm::server::PolicyPeer legacy_policy(
      legacy_wm_socket, gw::protocol::x11::kScreenModel, false);
  synchronize(legacy_policy);
  require(!legacy_policy.result().bindings && legacy_policy.policy_hash() != 0,
          "historical policy profile retains a bindings-free v1 snapshot");
  stop(legacy_wm);
  const auto compositor =
      launch(argv[2], comp_socket, "--dump-dir", root + "/dump");
  glasswyrm::server::CompositorPeer display(comp_socket,
                                            gw::protocol::x11::kScreenModel);
  synchronize(display);
  for (std::size_t index = 0; index < results.size(); ++index) {
    require(display.submit(project(index + 2, results[index]), error),
            error.c_str());
    drive(display);
  }
  stop(compositor);
  for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
    require(entry.path().extension() != ".ppm",
            "metadata sequence creates no PPM");

  const std::string session_socket = root + "/gwcomp-session.sock";
  const auto session_compositor =
      launch(argv[2], session_socket, "--dump-dir", root + "/session-dump");
  glasswyrm::server::CompositorPeer session_peer(
      session_socket, gw::protocol::x11::kScreenModel, false, true);
  synchronize(session_peer);
  require(session_peer.state() ==
              glasswyrm::server::PeerBootstrapState::Synchronized,
          "real-input profile negotiates compositor session-state support");
  stop(session_compositor);

  const std::string buffered_socket = root + "/gwcomp-buffered.sock";
  auto pixels = glasswyrm::server::PixelStorage::create(32, 24);
  require(pixels.has_value(), "create restart pixels");
  pixels->fill({0, 0, 32, 24}, 0x0000aa44U);
  auto buffer = glasswyrm::server::PublishedWindowBuffer::create(
      1, 100, *pixels);
  require(buffer != nullptr, "create restart publication buffer");
  auto first_compositor =
      launch(argv[2], buffered_socket, "--dump-dir", root + "/buffered-1");
  glasswyrm::server::CompositorPeer buffered(
      buffered_socket, gw::protocol::x11::kScreenModel, true);
  synchronize(buffered);
  require(buffered.submit(buffered_project(2, buffer.get()), error),
          error.c_str());
  drive(buffered);
  require(buffered.submit(buffered_project(3, nullptr), error), error.c_str());
  drive(buffered);
  require(buffered.replay_input().buffers.size() == 1 &&
              buffered.replay_input().buffers.front().attach.buffer_id == 1,
          "accepted attachment omission retains complete replay state");
  glasswyrm::server::CursorPresenter cursor_presenter;
  auto cursor_image = glasswyrm::input::make_glyph_cursor(
      {glasswyrm::input::CursorFontIdentity::Cursor,
       glasswyrm::input::CursorFontIdentity::Cursor,
       glasswyrm::input::kCursorGlyphLeftPointer,
       static_cast<std::uint16_t>(
           glasswyrm::input::kCursorGlyphLeftPointer + 1U),
       {0xffff, 0xffff, 0xffff}, {0, 0, 0}},
      error);
  require(cursor_image != nullptr, error.c_str());
  glasswyrm::server::CompositorCursorSubmission cursor;
  require(cursor_presenter.prepare(cursor_image, 10, 12, true, cursor, error) &&
              buffered.submit_cursor(cursor, 4, 4, error),
          error.c_str());
  drive(buffered);
  cursor_presenter.accept();
  glasswyrm::server::CompositorCursorSubmission moved;
  require(cursor_presenter.prepare(cursor_image, 18, 20, true, moved, error) &&
              !moved.buffer && buffered.submit_cursor(moved, 5, 5, error),
          error.c_str());
  drive(buffered);
  cursor_presenter.accept();
  require(buffered.submit(buffered_project(6, nullptr), error), error.c_str());
  drive(buffered);
  require(buffered.replay_input().surfaces.size() == 2 &&
              buffered.replay_input().policies.size() == 1 &&
              buffered.replay_input().buffers.size() == 2,
          "lifecycle snapshots retain the accepted policy-free cursor and buffer");
  stop(first_compositor);
  auto second_compositor =
      launch(argv[2], buffered_socket, "--dump-dir", root + "/buffered-2");
  synchronize(buffered);
  require(buffered.state() ==
              glasswyrm::server::PeerBootstrapState::Synchronized,
          "buffered peer reconnect replays retained attachment");
  stop(second_compositor);
  return 0;
}
