#include "glasswyrmd/compositor_peer.hpp"
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
  return 0;
}
