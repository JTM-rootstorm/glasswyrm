#include "glasswyrmd/compositor_peer.hpp"
#include "glasswyrmd/cursor_presenter.hpp"
#include "glasswyrmd/pixel_storage.hpp"
#include "glasswyrmd/published_buffer.hpp"
#include "glasswyrmd/policy_peer.hpp"

#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <poll.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <utility>
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
pid_t launch_output_model(const char *executable, const std::string &socket,
                          const std::string &dump,
                          const char *second_output) {
  const auto child = ::fork();
  require(child >= 0, "fork output-model compositor");
  if (child == 0) {
    ::execl(executable, executable, "--ipc-socket", socket.c_str(),
            "--dump-dir", dump.c_str(), "--headless-output",
            "LEFT:800x600@60000", "--headless-output", second_output,
            nullptr);
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
void connect_for_output_snapshot(glasswyrm::server::PolicyPeer &peer) {
  std::string error;
  while (!peer.connect(error))
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!peer.ready_for_snapshot()) {
    require(std::chrono::steady_clock::now() < deadline,
            "output policy handshake timed out");
    pollfd descriptor{peer.fd(), peer.wanted_events(), 0};
    require(::poll(&descriptor, 1, 50) >= 0, "poll output policy peer");
    require(peer.process(descriptor.revents, error) ==
                glasswyrm::server::PeerProcessOutcome::Progress,
            error.c_str());
  }
}
gwipc_policy_output_upsert policy_output(
    const std::uint64_t id, const std::int32_t x, const std::uint32_t width,
    const std::uint32_t height, const std::uint32_t scale_numerator,
    const std::uint32_t scale_denominator, const bool primary) {
  gwipc_policy_output_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = id;
  value.logical_x = value.work_x = x;
  value.logical_width = value.work_width = width;
  value.logical_height = value.work_height = height;
  value.scale_numerator = scale_numerator;
  value.scale_denominator = scale_denominator;
  value.transform = GWIPC_TRANSFORM_NORMAL;
  value.enabled = 1;
  value.primary = primary;
  return value;
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
    attachment.attach.synchronization = buffer->synchronization();
    attachment.fd = buffer->fd();
    attachment.synchronization_fd = buffer->synchronization_fd();
    submission.buffers.push_back(attachment);
    glasswyrm::server::CompositorSnapshotSubmission::Damage damage;
    damage.surface_id = surface.surface_id;
    damage.rectangles.push_back({0, 0, buffer->width(), buffer->height()});
    submission.damages.push_back(std::move(damage));
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
    require(policy.submit({id, id, {window}, {}, {}}, error), error.c_str());
    drive(policy);
    results.push_back(policy.result());
  }
  require(policy.submit({6, 6, {}, {}, {}}, error), error.c_str());
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

  const std::string output_wm_socket = root + "/gwm-output.sock";
  const auto output_wm = launch(argv[1], output_wm_socket);
  auto output_screen = gw::protocol::x11::kScreenModel;
  output_screen.width_pixels = 1440;
  output_screen.height_pixels = 600;
  output_screen.width_millimeters = 381;
  output_screen.height_millimeters = 159;
  glasswyrm::server::PolicyPeer output_policy(output_wm_socket, output_screen,
                                               true, true);
  connect_for_output_snapshot(output_policy);
  glasswyrm::server::PolicySnapshotSubmission output_submission{
      1,
      1,
      {},
      {policy_output(11, 0, 800, 600, 1, 1, true),
       policy_output(12, 800, 640, 480, 3, 2, false)},
      {}};
  require(output_policy.submit(output_submission, error), error.c_str());
  drive(output_policy);
  const auto output_hash = output_policy.policy_hash();
  require(output_hash != 0 && output_policy.result().windows.empty() &&
              output_policy.result().bindings.has_value(),
          "zero-window multi-output policy snapshot returns a v3 hash");
  output_policy.disconnect();
  synchronize(output_policy);
  require(output_policy.policy_hash() == output_hash,
          "multi-output reconnect replays the exact v3 policy hash");
  auto hinted_window = lifecycle(2, true, 0, 0);
  hinted_window.geometry_serial = 0;
  gwipc_policy_window_output_hint output_hint{};
  output_hint.struct_size = sizeof(output_hint);
  output_hint.window_id = hinted_window.window.window_id;
  output_hint.preferred_output_id = 12;
  output_submission.commit_id = 2;
  output_submission.generation = 2;
  output_submission.windows = {hinted_window};
  output_submission.output_hints = {output_hint};
  require(output_policy.submit(output_submission, error), error.c_str());
  drive(output_policy);
  require(output_policy.result().windows.size() == 1 &&
              output_policy.result().windows.front().output_id == 12,
          "GWM consumes the preferred-output hint in its v3 transaction");
  stop(output_wm);

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
  require(display.output_layout() == nullptr,
          "historical compositor profile does not query output inventory");
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
      1, 100, *pixels, GWIPC_SYNCHRONIZATION_EVENTFD);
  require(buffer != nullptr, "create restart publication buffer");
  auto first_compositor =
      launch(argv[2], buffered_socket, "--dump-dir", root + "/buffered-1");
  glasswyrm::server::CompositorPeer buffered(
      buffered_socket, gw::protocol::x11::kScreenModel, true, false, true);
  synchronize(buffered);
  require(buffer->signal_ready() &&
              buffered.submit(buffered_project(2, buffer.get()), error),
          error.c_str());
  drive(buffered);
  errno = 0;
  require(!buffer->retract_ready() && errno == EAGAIN,
          "first compositor consumes the publication readiness token");
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
              buffered.replay_input().buffers.size() == 2 &&
              buffered.replay_input().damages.empty(),
          "replay retains cursor and buffers but no consumed one-shot damage");
  stop(first_compositor);
  auto second_compositor =
      launch(argv[2], buffered_socket, "--dump-dir", root + "/buffered-2");
  synchronize(buffered);
  require(buffered.state() ==
              glasswyrm::server::PeerBootstrapState::Synchronized,
          "buffered peer reconnect rearms and replays retained attachment");
  errno = 0;
  require(!buffer->retract_ready() && errno == EAGAIN,
          "reconnecting compositor consumes exactly one rearmed token");
  glasswyrm::server::CompositorContentSubmission resumed_content{7, 7, {}, {}};
  glasswyrm::server::CompositorSnapshotSubmission::Damage resumed_damage;
  resumed_damage.surface_id = (UINT64_C(1) << 32U) | 100U;
  resumed_damage.rectangles.push_back({0, 0, buffer->width(), buffer->height()});
  resumed_content.damages.push_back(std::move(resumed_damage));
  require(buffered.submit_content(resumed_content, error), error.c_str());
  drive(buffered);
  errno = 0;
  require(!buffer->retract_ready() && errno == EAGAIN,
          "resumed content rearms a token consumed by reconnect replay");
  auto replacement = glasswyrm::server::PublishedWindowBuffer::create(
      2, 100, *pixels, GWIPC_SYNCHRONIZATION_EVENTFD);
  require(replacement != nullptr && replacement->signal_ready(),
          "prepare interrupted replacement buffer");
  auto interrupted = buffered_project(8, replacement.get());
  require(replacement->retract_ready(),
          "simulate disconnect retracting replacement readiness");
  stop(second_compositor);
  auto third_compositor =
      launch(argv[2], buffered_socket, "--dump-dir", root + "/buffered-3");
  synchronize(buffered);
  require(buffered.submit(interrupted, error), error.c_str());
  drive(buffered);
  errno = 0;
  require(!replacement->retract_ready() && errno == EAGAIN,
          "resumed snapshot rearms its retracted replacement buffer");
  stop(third_compositor);
  require(replacement->signal_ready(),
          "pre-signal retained reconnect buffer");
  auto fourth_compositor =
      launch(argv[2], buffered_socket, "--dump-dir", root + "/buffered-4");
  synchronize(buffered);
  errno = 0;
  require(!replacement->retract_ready() && errno == EAGAIN,
          "reconnect normalizes an existing readiness token without doubling");
  stop(fourth_compositor);

  const std::string output_model_socket = root + "/gwcomp-output-model.sock";
  auto output_model_compositor = launch_output_model(
      argv[2], output_model_socket, root + "/output-model-1",
      "RIGHT:640x480@75000");
  glasswyrm::server::CompositorPeer output_model_peer(
      output_model_socket, gw::protocol::x11::kScreenModel, false, false,
      false, true);
  synchronize(output_model_peer);
  const auto *initial_layout = output_model_peer.output_layout();
  require(initial_layout != nullptr && initial_layout->descriptors.size() == 2 &&
              initial_layout->states.size() == 2 &&
              initial_layout->enabled_output_count == 2 &&
              initial_layout->root_logical_width == 1440 &&
              initial_layout->root_logical_height == 600,
          "output-model bootstrap exposes the validated compositor inventory");
  const auto initial_generation = initial_layout->generation;
  auto adopted_layout = *initial_layout;
  const auto right = std::ranges::find_if(
      adopted_layout.descriptors, [](const auto &item) {
        return item.second.name == "RIGHT";
      });
  require(right != adopted_layout.descriptors.end(),
          "find reconnect output by stable name");
  auto &right_state = adopted_layout.states.at(right->first);
  ++adopted_layout.generation;
  right_state.scale = {5, 4};
  right_state.logical_width = 512;
  right_state.logical_height = 384;
  right_state.generation = adopted_layout.generation;
  for (auto &[unused, state] : adopted_layout.states) {
    (void)unused;
    state.generation = adopted_layout.generation;
  }
  adopted_layout.root_logical_width = 1312;
  require(static_cast<bool>(glasswyrm::output::validate_layout(adopted_layout)) &&
              output_model_peer.adopt_output_layout(adopted_layout),
          "adopt a non-default committed layout before reconnect");
  stop(output_model_compositor);
  output_model_compositor = launch_output_model(
      argv[2], output_model_socket, root + "/output-model-2",
      "RIGHT:640x480@75000");
  synchronize(output_model_peer);
  const auto *reconnected_layout = output_model_peer.output_layout();
  require(reconnected_layout != nullptr &&
              reconnected_layout->descriptors.size() == 2 &&
              reconnected_layout->generation == initial_generation + 1 &&
              reconnected_layout->root_logical_width == 1312 &&
              reconnected_layout->states.at(right->first).scale ==
                  glasswyrm::output::RationalScale{5, 4},
          "stable reconnect inventory preserves the committed layout");
  stop(output_model_compositor);

  output_model_compositor = launch_output_model(
      argv[2], output_model_socket, root + "/output-model-changed",
      "RIGHT:1024x768@60000");
  std::string reconnect_error;
  while (!output_model_peer.connect(reconnect_error))
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  const auto reconnect_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  auto reconnect_outcome = glasswyrm::server::PeerProcessOutcome::Progress;
  while (reconnect_outcome == glasswyrm::server::PeerProcessOutcome::Progress) {
    require(std::chrono::steady_clock::now() < reconnect_deadline,
            "changed output inventory timed out");
    pollfd descriptor{output_model_peer.fd(), output_model_peer.wanted_events(),
                      0};
    require(::poll(&descriptor, 1, 50) >= 0,
            "poll changed output inventory");
    reconnect_outcome =
        output_model_peer.process(descriptor.revents, reconnect_error);
  }
  require(reconnect_outcome == glasswyrm::server::PeerProcessOutcome::Fatal &&
              reconnect_error ==
                  "compositor output inventory changed across reconnect",
          "descriptor or mode drift is a deterministic fatal divergence");
  stop(output_model_compositor);
  return 0;
}
