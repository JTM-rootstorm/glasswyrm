#include "glasswyrmd/compositor_peer.hpp"
#include "glasswyrmd/compositor_buffer_replay.hpp"
#include "glasswyrmd/cursor_presenter.hpp"
#include "glasswyrmd/lifecycle_projection.hpp"
#include "glasswyrmd/output_scene_projection.hpp"
#include "glasswyrmd/pixel_storage.hpp"
#include "glasswyrmd/published_buffer.hpp"
#include "input/cursor_model.hpp"
#include "tests/helpers/test_support.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <poll.h>
#include <string>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

using glasswyrm::server::CompositorContentSubmission;
using glasswyrm::server::CompositorPeer;
using glasswyrm::server::CompositorSnapshotSubmission;
using glasswyrm::server::PeerBootstrapState;
using glasswyrm::server::PeerProcessOutcome;
using glasswyrm::server::PublishedWindowBuffer;
using gw::test::require;

constexpr std::uint32_t kWindow = 42;
constexpr std::uint64_t kSurface = (UINT64_C(1) << 32U) | kWindow;
constexpr std::uint32_t kWidth = 32;
constexpr std::uint32_t kHeight = 24;

struct Child {
  pid_t pid{-1};

  Child() = default;
  explicit Child(const pid_t value) : pid(value) {}
  Child(const Child &) = delete;
  Child &operator=(const Child &) = delete;

  ~Child() { stop(); }

  void stop() noexcept {
    if (pid <= 0)
      return;
    (void)::kill(pid, SIGTERM);
    (void)::waitpid(pid, nullptr, 0);
    pid = -1;
  }
};

Child launch(const char *executable, const std::string &socket,
             const std::string &dump) {
  const auto child = ::fork();
  require(child >= 0, "fork focused M14 compositor");
  if (child == 0) {
    (void)::prctl(PR_SET_PDEATHSIG, SIGTERM);
    ::execl(executable, executable, "--ipc-socket", socket.c_str(),
            "--dump-dir", dump.c_str(), "--headless-output",
            "FOCUSED:64x64@60000", "--headless-vrr", "FOCUSED=40000-60000",
            nullptr);
    _exit(127);
  }
  return Child(child);
}

void drive(CompositorPeer &peer, const char *timeout_message) {
  std::string error;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (peer.state() != PeerBootstrapState::Synchronized) {
    require(std::chrono::steady_clock::now() < deadline, timeout_message);
    pollfd descriptor{peer.fd(), peer.wanted_events(), 0};
    require(::poll(&descriptor, 1, 25) >= 0, "poll focused M14 peer");
    require(peer.process(descriptor.revents, error) ==
                PeerProcessOutcome::Progress,
            "focused M14 peer processing failed: " + error);
  }
}

void synchronize(CompositorPeer &peer) {
  std::string error;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!peer.connect(error)) {
    require(std::chrono::steady_clock::now() < deadline,
            "focused M14 peer connect timed out");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  drive(peer, "focused M14 inventory bootstrap timed out");
}

CompositorSnapshotSubmission::Buffer
attachment(const PublishedWindowBuffer &buffer) {
  CompositorSnapshotSubmission::Buffer result;
  result.attach.struct_size = sizeof(result.attach);
  result.attach.buffer_id = buffer.buffer_id();
  result.attach.surface_id = kSurface;
  result.attach.width = buffer.width();
  result.attach.height = buffer.height();
  result.attach.stride = buffer.stride();
  result.attach.storage_size = buffer.size();
  result.attach.pixel_format = GWIPC_PIXEL_FORMAT_XRGB8888;
  result.attach.alpha_semantics = GWIPC_ALPHA_OPAQUE;
  result.attach.color.color_space = GWIPC_SDR_COLOR_SPACE_SRGB;
  result.attach.color.transfer_function = GWIPC_TRANSFER_FUNCTION_SRGB;
  result.attach.color.primaries = GWIPC_COLOR_PRIMARIES_SRGB;
  result.attach.synchronization = buffer.synchronization();
  result.fd = buffer.fd();
  result.synchronization_fd = buffer.synchronization_fd();
  return result;
}

CompositorSnapshotSubmission::Damage damage() {
  CompositorSnapshotSubmission::Damage result;
  result.surface_id = kSurface;
  result.rectangles.push_back({0, 0, kWidth, kHeight});
  return result;
}

glasswyrm::server::LifecycleSnapshot snapshot(const std::uint64_t output_id,
                                              const std::int32_t x,
                                              const std::int32_t y) {
  glasswyrm::server::LifecycleSnapshot result;
  glasswyrm::server::LifecycleWindow window;
  window.xid = kWindow;
  window.parent = result.root_window;
  window.override_redirect = false;
  window.applied_x = x;
  window.applied_y = y;
  window.applied_width = kWidth;
  window.applied_height = kHeight;
  window.stacking = 0;
  window.policy_visible = true;
  window.focused = true;
  window.managed = true;
  window.window_type = GWIPC_POLICY_WINDOW_NORMAL;
  window.applied_state = GWIPC_POLICY_APPLIED_NORMAL;
  window.assigned_output_id = output_id;
  window.output_memberships = {output_id};
  result.windows.emplace(kWindow, std::move(window));
  result.root_order = {kWindow};
  return result;
}

} // namespace

int main(const int argc, char **argv) {
  require(argc == 2, "expected gwcomp path");
  char temporary[] = "/tmp/compositor-peer-vrr-release-XXXXXX";
  require(::mkdtemp(temporary) != nullptr, "create focused M14 directory");
  const std::filesystem::path root(temporary);
  const auto socket = (root / "gwcomp.sock").string();
  auto compositor = launch(argv[1], socket, (root / "dump").string());

  CompositorPeer peer(socket, gw::protocol::x11::kScreenModel, true, false,
                      true, true, true);
  synchronize(peer);
  const auto *layout = peer.output_layout();
  auto *vrr = peer.vrr_cache();
  require(layout && vrr && layout->enabled_output_count == 1,
          "focused M14 peer exposes one VRR output");
  const auto output_id = layout->primary_output_id.value;
  const auto &output = layout->states.at(layout->primary_output_id);
  require(vrr->set_policy(output_id, GWIPC_VRR_POLICY_FOCUSED),
          "select focused M14 output policy");
  vrr->set_window_preference(kWindow, GWIPC_VRR_PREFERENCE_DEFAULT);
  gwipc_policy_output_vrr_state output_policy{};
  output_policy.struct_size = sizeof(output_policy);
  output_policy.output_id = output_id;
  output_policy.mode = GWIPC_VRR_POLICY_FOCUSED;
  output_policy.candidate_required = 1;
  output_policy.reason_flags = GWIPC_VRR_REASON_NO_CANDIDATE;
  gwipc_policy_window_vrr_state window_policy{};
  window_policy.struct_size = sizeof(window_policy);
  window_policy.window_id = kWindow;
  window_policy.output_id = output_id;
  window_policy.preference = GWIPC_VRR_PREFERENCE_DEFAULT;
  window_policy.focused = 1;
  window_policy.reason_flags = GWIPC_VRR_REASON_SURFACE_MEMBERSHIP_INVALID;
  require(vrr->stage_policy_result(2, {output_policy}, {window_policy}),
          "stage membership-invalid focused M14 policy result");

  auto pixels = glasswyrm::server::PixelStorage::create(kWidth, kHeight);
  require(pixels.has_value(), "create focused M14 pixels");
  pixels->fill({0, 0, kWidth, kHeight}, UINT32_C(0x00102030));
  auto initial = PublishedWindowBuffer::create(
      100, kWindow, *pixels, GWIPC_SYNCHRONIZATION_EVENTFD);
  require(initial != nullptr, "create initial focused M14 buffer");
  require(initial->signal_ready(), "signal initial focused M14 buffer");

  auto first = glasswyrm::server::project_compositor(
      snapshot(output_id, output.logical_x, output.logical_y), 2, 2, true,
      layout, vrr);
  first.buffers.push_back(attachment(*initial));
  first.damages.push_back(damage());
  std::string error;
  require(peer.submit(first, error), "initial M14 submit failed: " + error);
  drive(peer, "initial M14 buffer waited for a nonexistent release");
  require(peer.take_releases().empty() &&
              peer.vrr_response().released_buffer_ids.empty() &&
              peer.replay_input().buffers.size() == 1 &&
              peer.replay_input().buffers.front().attach.buffer_id == 100 &&
              vrr->expectation() == nullptr,
          "initial M14 buffer completes without releasing itself");

  CompositorContentSubmission content;
  content.commit_id = 3;
  content.generation = 3;
  content.damages.push_back(damage());
  const bool content_submitted = peer.submit_content(content, error);
  require(content_submitted, "retained M14 content submit failed: " + error);
  drive(peer, "retained M14 content waited for a nonexistent release");
  require(peer.take_releases().empty() &&
              peer.vrr_response().released_buffer_ids.empty() &&
              peer.replay_input().buffers.size() == 1 &&
              peer.replay_input().buffers.front().attach.buffer_id == 100 &&
              vrr->expectation() == nullptr,
          "retained M14 content completes without releasing its buffer");

  pixels->fill({0, 0, kWidth, kHeight}, UINT32_C(0x00405060));
  auto replacement = PublishedWindowBuffer::create(
      101, kWindow, *pixels, GWIPC_SYNCHRONIZATION_EVENTFD);
  require(replacement != nullptr, "create replacement focused M14 buffer");
  require(replacement->signal_ready(),
          "signal replacement focused M14 buffer");
  CompositorContentSubmission replacement_content;
  replacement_content.commit_id = 4;
  replacement_content.generation = 4;
  replacement_content.buffers.push_back(attachment(*replacement));
  replacement_content.damages.push_back(damage());
  const bool replacement_submitted =
      peer.submit_content(replacement_content, error);
  require(replacement_submitted,
          "replacement M14 content submit failed: " + error);
  drive(peer, "replacement M14 buffer release timed out");
  const auto releases = peer.take_releases();
  require(releases.size() == 1 && releases.front().buffer_id == 100 &&
              releases.front().reason == GWIPC_BUFFER_RELEASE_REPLACED &&
              peer.vrr_response().released_buffer_ids ==
                  std::vector<std::uint64_t>{100} &&
              peer.replay_input().buffers.size() == 1 &&
              peer.replay_input().buffers.front().attach.buffer_id == 101 &&
              vrr->expectation() == nullptr,
          "replacement releases only the retired prior M14 buffer");

  glasswyrm::server::CursorPresenter cursor_presenter;
  auto cursor_image = glasswyrm::input::make_glyph_cursor(
      {glasswyrm::input::CursorFontIdentity::Cursor,
       glasswyrm::input::CursorFontIdentity::Cursor,
       glasswyrm::input::kCursorGlyphLeftPointer,
       static_cast<std::uint16_t>(
           glasswyrm::input::kCursorGlyphLeftPointer + 1U),
       {0xffff, 0xffff, 0xffff}, {0, 0, 0}},
      error);
  require(cursor_image != nullptr, "create focused M14 cursor: " + error);
  glasswyrm::server::CompositorCursorSubmission cursor;
  require(cursor_presenter.prepare(cursor_image, 4, 5, true, cursor, error) &&
              glasswyrm::server::populate_cursor_output_state(cursor,
                                                               *layout) &&
              peer.submit_cursor(cursor, 5, 5, error),
          "publish focused M14 cursor: " + error);
  drive(peer, "focused M14 cursor publication timed out");
  cursor_presenter.accept();
  require(peer.replay_input().surfaces.size() == 2 &&
              peer.replay_input().buffers.size() == 2 &&
              peer.replay_input().damages.empty(),
          "focused M14 replay retains window and cursor buffers");

  compositor.stop();
  peer.disconnect();
  auto replacement_compositor =
      launch(argv[1], socket, (root / "restart-dump").string());
  synchronize(peer);
  auto replay = peer.replay_input();
  require(glasswyrm::server::compositor_buffer_replay::prepare(replay, error),
          "prepare retained M14 reconnect replay: " + error);
  require(peer.submit(replay, error),
          "submit retained M14 reconnect replay: " + error);
  drive(peer, "retained M14 reconnect replay timed out");
  require(peer.state() == PeerBootstrapState::Synchronized &&
              peer.replay_input().buffers.size() == 2 &&
              std::ranges::any_of(
                  peer.replay_input().buffers,
                  [](const auto &buffer) {
                    return buffer.attach.buffer_id == 101;
                  }) &&
              peer.take_releases().empty() && vrr->expectation() == nullptr,
          "retained M14 buffer survives compositor reconnect replay");

  peer.disconnect();
  replacement_compositor.stop();
  std::error_code cleanup_error;
  std::filesystem::remove_all(root, cleanup_error);
  require(!cleanup_error, "remove focused M14 directory");
  return 0;
}
