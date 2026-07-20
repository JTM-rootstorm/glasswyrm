#include "glasswyrmd/runtime_bridge.hpp"
#include "glasswyrmd/cursor_presenter.hpp"
#include "glasswyrmd/lifecycle_projection.hpp"
#include "glasswyrmd/output_scene_projection.hpp"
#include "glasswyrmd/pixel_storage.hpp"
#include "glasswyrmd/published_buffer.hpp"
#include "glasswyrmd/vrr_policy_projection.hpp"
#include "tests/helpers/test_support.hpp"

#include <chrono>
#include <csignal>
#include <poll.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
using glasswyrm::server::RuntimeBridge;
using gw::test::require;

pid_t launch(const char* executable, const std::string& socket,
             const std::string& dump = {}) {
  const auto child = ::fork();
  require(child >= 0, "fork bridge peer");
  if (child == 0) {
    if (dump.empty())
      ::execl(executable, executable, "--ipc-socket", socket.c_str(), nullptr);
    else
      ::execl(executable, executable, "--ipc-socket", socket.c_str(),
              "--dump-dir", dump.c_str(), nullptr);
    _exit(127);
  }
  return child;
}

pid_t launch_output_model(const char* executable, const std::string& socket,
                          const std::string& dump) {
  const auto child = ::fork();
  require(child >= 0, "fork output-model bridge peer");
  if (child == 0) {
    ::execl(executable, executable, "--ipc-socket", socket.c_str(),
            "--dump-dir", dump.c_str(), "--headless-output",
            "LEFT:800x600@60000", "--headless-output",
            "RIGHT:640x480@75000", nullptr);
    _exit(127);
  }
  return child;
}

pid_t launch_output_model_vrr(const char* executable,
                              const std::string& socket,
                              const std::string& dump) {
  const auto child = ::fork();
  require(child >= 0, "fork VRR output-model bridge peer");
  if (child == 0) {
    ::execl(executable, executable, "--ipc-socket", socket.c_str(),
            "--dump-dir", dump.c_str(), "--headless-output",
            "LEFT:800x600@60000", "--headless-output",
            "RIGHT:640x480@75000", "--headless-vrr",
            "LEFT=40000-60000", "--headless-vrr", "RIGHT=48000-75000",
            nullptr);
    _exit(127);
  }
  return child;
}

void stop(const pid_t child) {
  if (child <= 0) return;
  (void)::kill(child, SIGTERM);
  int status = 0;
  require(::waitpid(child, &status, 0) == child, "reap bridge peer");
}

void stop_while_suspended(const pid_t child) {
  require(child > 0 && ::kill(child, SIGKILL) == 0,
          "kill suspended bridge peer");
  int status = 0;
  require(::waitpid(child, &status, 0) == child, "reap suspended bridge peer");
}

bool service_once(RuntimeBridge& bridge, std::string& error) {
  pollfd descriptors[2]{{bridge.policy_fd(), bridge.policy_events(), 0},
                        {bridge.compositor_fd(), bridge.compositor_events(), 0}};
  require(::poll(descriptors, 2, 50) >= 0, "poll bridge transaction");
  return bridge.service(descriptors[0].revents, descriptors[1].revents,
                        RuntimeBridge::Clock::now(), error);
}

template <class Predicate>
void drive_until(RuntimeBridge& bridge, Predicate predicate,
                 const char* message) {
  std::string error;
  const auto deadline =
      RuntimeBridge::Clock::now() + std::chrono::seconds(5);
  while (!predicate()) {
    require(RuntimeBridge::Clock::now() < deadline, message);
    require(service_once(bridge, error), error.c_str());
  }
}

}  // namespace

int main(int argc, char** argv) {
  require(argc == 3, "expected gwm and gwcomp paths");
  char temporary[] = "/tmp/runtime-bridge-transaction-XXXXXX";
  require(::mkdtemp(temporary) != nullptr, "create bridge directory");
  const std::string root = temporary;
  const std::string policy_socket = root + "/gwm.sock";
  const std::string compositor_socket = root + "/gwcomp.sock";
  auto policy_process = launch(argv[1], policy_socket);
  const auto compositor_process =
      launch(argv[2], compositor_socket, root + "/dump");

  RuntimeBridge bridge(policy_socket, compositor_socket,
                       gw::protocol::x11::kScreenModel);
  bridge.start();
  drive_until(bridge, [&] { return bridge.ready(); },
              "bridge bootstrap timed out");

  std::string error;
  require(bridge.submit_policy({2, 2, {}, {}, {}}, error) &&
              !bridge.submit_policy({3, 3, {}, {}, {}}, error),
          "only one policy transaction may be staged");
  drive_until(bridge, [&] { return bridge.policy_result_ready(); },
              "policy result timed out");
  require(bridge.policy_result().generation == 2 &&
              bridge.prepare_rollback() &&
              bridge.submit_policy({3, 3, {}, {}, {}}, error),
          "policy-ready transaction can explicitly begin rollback");
  drive_until(bridge, [&] { return bridge.policy_result_ready(); },
              "rollback policy result timed out");
  require(bridge.submit_compositor({3, 3, {}, {}, {}, {}}, error),
          "accepted rollback policy advances to compositor replay");
  drive_until(bridge, [&] { return bridge.compositor_result_ready(); },
              "compositor result timed out");
  require(bridge.prepare_rollback() &&
              bridge.submit_policy({4, 4, {}, {}, {}}, error),
          "completed transaction can explicitly begin policy rollback");

  const auto compositor_fd_before_policy_restart = bridge.compositor_fd();
  stop(policy_process);
  drive_until(bridge, [&] { return !bridge.ready(); },
              "policy disconnect was not reported");
  require(bridge.compositor_fd() == compositor_fd_before_policy_restart,
          "policy restart preserves the healthy compositor transport");
  policy_process = launch(argv[1], policy_socket);
  drive_until(bridge, [&] { return bridge.policy_result_ready(); },
              "peer restart replays bootstrap and pending policy transaction");
  require(bridge.policy_result().generation == 4,
          "restarted policy peer completes the retained transaction");

  gwipc_policy_lifecycle_window_upsert unsupported{};
  unsupported.struct_size = sizeof(unsupported);
  unsupported.window.struct_size = sizeof(unsupported.window);
  unsupported.window.window_id = 100;
  unsupported.window.parent_window_id = 1;
  unsupported.window.workspace_id = 2;
  unsupported.window.requested_width = 64;
  unsupported.window.requested_height = 64;
  unsupported.window.window_type = GWIPC_POLICY_WINDOW_NORMAL;
  unsupported.window.map_intent = GWIPC_POLICY_UNMAPPED;
  unsupported.window.decoration_preference = GWIPC_TRI_STATE_UNKNOWN;
  unsupported.window.creation_serial = 1;
  unsupported.stack_mode = GWIPC_POLICY_STACK_NONE;
  require(bridge.prepare_rollback() &&
              bridge.submit_policy({5, 5, {unsupported}, {}, {}}, error),
          "submit wire-valid unsupported policy metadata");
  drive_until(bridge, [&] { return bridge.policy_rejected_ready(); },
              "semantic policy rejection timed out");
  require(bridge.prepare_rollback(),
          "semantic rejection remains available to coordinator rollback");

  bridge.start();
  require(!bridge.policy_result_ready() &&
              !bridge.compositor_result_ready() &&
              !bridge.prepare_rollback() &&
              !bridge.submit_policy({5, 5, {}, {}, {}}, error),
          "start resets transaction stage before peers synchronize");
  stop(compositor_process);
  stop(policy_process);

  const std::string cursor_policy_socket = root + "/gwm-cursor.sock";
  const std::string cursor_compositor_socket = root + "/gwcomp-cursor.sock";
  const auto cursor_policy_process = launch(argv[1], cursor_policy_socket);
  auto cursor_compositor_process =
      launch(argv[2], cursor_compositor_socket, root + "/cursor-dump");
  RuntimeBridge cursor_bridge(cursor_policy_socket, cursor_compositor_socket,
                              gw::protocol::x11::kScreenModel,
                              std::chrono::seconds(10), true);
  cursor_bridge.start();
  drive_until(cursor_bridge, [&] { return cursor_bridge.ready(); },
              "buffered cursor bridge bootstrap timed out");
  glasswyrm::server::CursorPresenter presenter;
  auto image = glasswyrm::input::make_glyph_cursor(
      {glasswyrm::input::CursorFontIdentity::Cursor,
       glasswyrm::input::CursorFontIdentity::Cursor,
       glasswyrm::input::kCursorGlyphLeftPointer,
       static_cast<std::uint16_t>(
           glasswyrm::input::kCursorGlyphLeftPointer + 1U),
       {0xffff, 0xffff, 0xffff}, {0, 0, 0}},
      error);
  require(image != nullptr, error.c_str());
  glasswyrm::server::CompositorCursorSubmission cursor;
  require(presenter.prepare(image, 7, 9, true, cursor, error) &&
              cursor_bridge.submit_cursor(cursor, 2, 2, error) &&
              !cursor_bridge.submit_cursor(cursor, 3, 3, error),
          "cursor update serializes behind the active compositor frame");
  drive_until(cursor_bridge, [&] { return cursor_bridge.cursor_result_ready(); },
              "cursor frame result timed out");
  presenter.accept();
  cursor_bridge.clear_transaction_result();
  require(cursor_bridge.transaction_idle(),
          "accepted cursor frame returns the bridge to idle");
  const auto disconnected_buffer = cursor.buffer->attach.buffer_id;
  const auto policy_fd_before_compositor_restart = cursor_bridge.policy_fd();
  stop(cursor_compositor_process);
  drive_until(cursor_bridge,
              [&] { return cursor_bridge.take_compositor_reset(); },
              "cursor compositor disconnect was not reported");
  require(cursor_bridge.policy_fd() == policy_fd_before_compositor_restart,
          "compositor restart preserves the healthy policy transport");
  cursor_bridge.forget_cursor_replay();
  presenter.peer_disconnected();
  require(presenter.needs_update(image, 7, 9, true),
          "cursor presenter requires a fresh reconnect publication");
  cursor_compositor_process = launch(argv[2], cursor_compositor_socket,
                                     root + "/cursor-reconnect-dump");
  drive_until(cursor_bridge, [&] { return cursor_bridge.ready(); },
              "cursor bridge reconnect timed out");
  glasswyrm::server::CompositorCursorSubmission reconnected;
  require(presenter.prepare(image, 7, 9, true, reconnected, error) &&
              reconnected.buffer && reconnected.damage &&
              reconnected.buffer->attach.buffer_id != disconnected_buffer &&
              cursor_bridge.submit_cursor(reconnected, 3, 3, error),
          "reconnect submits a newly owned cursor buffer");
  drive_until(cursor_bridge, [&] { return cursor_bridge.cursor_result_ready(); },
              "reconnected cursor frame result timed out");
  presenter.accept();
  cursor_bridge.clear_transaction_result();
  require(cursor_bridge.transaction_idle(),
          "reconnected cursor frame returns the bridge to idle");
  stop(cursor_compositor_process);
  stop(cursor_policy_process);

  const std::string output_policy_socket = root + "/gwm-output-model.sock";
  const std::string output_compositor_socket =
      root + "/gwcomp-output-model.sock";
  const auto output_policy_process = launch(argv[1], output_policy_socket);
  const auto output_compositor_process = launch_output_model(
      argv[2], output_compositor_socket, root + "/output-model-dump");
  RuntimeBridge output_bridge(
      output_policy_socket, output_compositor_socket,
      gw::protocol::x11::kScreenModel, std::chrono::seconds(10), true, false,
      false, true);
  output_bridge.start();
  drive_until(output_bridge, [&] { return output_bridge.ready(); },
              "output-model bridge inventory bootstrap timed out");
  const auto* output_layout = output_bridge.output_layout();
  require(output_layout != nullptr && output_layout->descriptors.size() == 2 &&
              output_layout->enabled_output_count == 2 &&
              output_layout->root_logical_width == 1440 &&
              output_layout->root_logical_height == 600,
          "output-model bridge exposes compositor-authoritative layout");
  glasswyrm::server::CursorPresenter output_cursor_presenter;
  glasswyrm::server::CompositorCursorSubmission output_cursor;
  const auto output_cursor_scale =
      glasswyrm::server::cursor_buffer_scale(*output_layout, 900, 100);
  require(output_cursor_presenter.prepare(image, 900, 100, true, output_cursor,
                                          error, false,
                                          output_cursor_scale) &&
              output_bridge.submit_cursor(output_cursor, 2, 2, error),
          "first output-model cursor publication is accepted for submission");
  drive_until(output_bridge,
              [&] { return output_bridge.cursor_result_ready(); },
              "first output-model cursor frame timed out");
  output_cursor_presenter.accept();
  output_bridge.clear_transaction_result();
  require(output_bridge.transaction_idle(),
          "accepted output-model cursor frame returns the bridge to idle");
  glasswyrm::server::CompositorCursorSubmission moved_output_cursor;
  require(output_cursor_presenter.prepare(image, 901, 101, true,
                                          moved_output_cursor, error, false,
                                          output_cursor_scale) &&
              !moved_output_cursor.buffer &&
              output_bridge.submit_cursor(moved_output_cursor, 3, 3, error),
          "second same-layout cursor snapshot advances producer generation");
  drive_until(output_bridge,
              [&] { return output_bridge.cursor_result_ready(); },
              "second same-layout output-model cursor frame timed out");
  output_cursor_presenter.accept();
  output_bridge.clear_transaction_result();
  require(output_bridge.transaction_idle(),
          "second same-layout output-model frame commits independently");

  auto changed_layout = *output_layout;
  ++changed_layout.generation;
  for (auto& [unused, state] : changed_layout.states) {
    (void)unused;
    state.generation = changed_layout.generation;
  }
  require(output_bridge.adopt_output_layout(changed_layout),
          "output-model bridge adopts the next layout generation");
  glasswyrm::server::LifecycleSnapshot empty_snapshot;
  const auto changed_scene = glasswyrm::server::project_compositor(
      empty_snapshot, 4, 4, true, &changed_layout);
  require(output_bridge.submit_replay(changed_scene, error),
          "layout change drops stale retained cursor membership");
  drive_until(output_bridge,
              [&] { return output_bridge.replay_result_ready(); },
              "layout-change compositor frame timed out");
  output_bridge.clear_transaction_result();
  require(output_bridge.transaction_idle(),
          "layout change without a stale cursor returns the bridge to idle");
  stop(output_compositor_process);
  stop(output_policy_process);

  const std::string vrr_policy_socket = root + "/gwm-vrr-reconnect.sock";
  const std::string vrr_compositor_socket = root + "/gwcomp-vrr-reconnect.sock";
  const auto vrr_policy_process = launch(argv[1], vrr_policy_socket);
  auto vrr_compositor_process = launch_output_model_vrr(
      argv[2], vrr_compositor_socket, root + "/vrr-reconnect-dump");
  RuntimeBridge vrr_bridge(
      vrr_policy_socket, vrr_compositor_socket,
      gw::protocol::x11::kScreenModel, std::chrono::seconds(10), true, false,
      true, true, true);
  vrr_bridge.start();
  drive_until(vrr_bridge, [&] { return vrr_bridge.ready(); },
              "VRR reconnect bridge bootstrap timed out");
  const auto* vrr_layout = vrr_bridge.output_layout();
  auto* vrr_cache = vrr_bridge.vrr_cache();
  require(vrr_layout && vrr_cache && vrr_cache->outputs().size() == 2,
          "VRR reconnect bridge exposes complete inventory");
  const auto changed_output = vrr_layout->output_order.front().value;

  require(vrr_cache->set_policy(changed_output, GWIPC_VRR_POLICY_FOCUSED),
          "restore focused policy for buffered reconnect");
  glasswyrm::server::LifecycleSnapshot reconnect_membership_snapshot;
  reconnect_membership_snapshot.root_order = {10};
  glasswyrm::server::LifecycleWindow reconnect_membership_window;
  reconnect_membership_window.xid = 10;
  reconnect_membership_window.parent =
      reconnect_membership_snapshot.root_window;
  reconnect_membership_window.requested_width = 320;
  reconnect_membership_window.requested_height = 200;
  reconnect_membership_window.map_requested = true;
  reconnect_membership_window.creation_serial = 1;
  reconnect_membership_window.map_serial = 2;
  reconnect_membership_snapshot.windows.emplace(
      10, reconnect_membership_window);
  glasswyrm::server::VrrWindowStateStore reconnect_published_vrr;
  glasswyrm::server::synchronize_vrr_windows(
      reconnect_membership_snapshot, reconnect_published_vrr, *vrr_cache);
  const auto reconnect_policy = glasswyrm::server::project_policy(
      reconnect_membership_snapshot, 20, 20, vrr_layout, vrr_cache);
  require(vrr_bridge.submit_policy(reconnect_policy, error),
          "submit membership-invalid buffered baseline policy");
  drive_until(vrr_bridge, [&] { return vrr_bridge.policy_result_ready(); },
              "membership-invalid buffered baseline policy timed out");
  const auto reconnect_snapshot = glasswyrm::server::apply_policy_result(
      reconnect_membership_snapshot, vrr_bridge.policy_result(), vrr_layout,
      vrr_cache);
  require(reconnect_snapshot &&
              !vrr_cache->windows().at(10).policy_result->eligible &&
              (vrr_cache->windows().at(10).policy_result->reason_flags &
               GWIPC_VRR_REASON_SURFACE_MEMBERSHIP_INVALID) != 0,
          "buffered baseline retains contract-valid invalid membership");
  auto pixels = glasswyrm::server::PixelStorage::create(320, 200);
  require(pixels.has_value(), "create buffered reconnect pixels");
  pixels->fill({0, 0, 320, 200}, UINT32_C(0x00102030));
  auto buffer = glasswyrm::server::PublishedWindowBuffer::create(
      100, 10, *pixels, GWIPC_SYNCHRONIZATION_EVENTFD);
  require(buffer && buffer->signal_ready(),
          "create synchronized buffered reconnect publication");
  auto baseline = glasswyrm::server::project_compositor(
      *reconnect_snapshot, 21, 21, true, vrr_layout, vrr_cache);
  glasswyrm::server::CompositorSnapshotSubmission::Buffer attachment;
  attachment.attach.struct_size = sizeof(attachment.attach);
  attachment.attach.buffer_id = buffer->buffer_id();
  attachment.attach.surface_id = (UINT64_C(1) << 32U) | 10U;
  attachment.attach.width = buffer->width();
  attachment.attach.height = buffer->height();
  attachment.attach.stride = buffer->stride();
  attachment.attach.storage_size = buffer->size();
  attachment.attach.pixel_format = GWIPC_PIXEL_FORMAT_XRGB8888;
  attachment.attach.alpha_semantics = GWIPC_ALPHA_OPAQUE;
  attachment.attach.color = {GWIPC_SDR_COLOR_SPACE_SRGB,
                             GWIPC_TRANSFER_FUNCTION_SRGB,
                             GWIPC_COLOR_PRIMARIES_SRGB, 0, 0, 0, 0};
  attachment.attach.synchronization = buffer->synchronization();
  attachment.fd = buffer->fd();
  attachment.synchronization_fd = buffer->synchronization_fd();
  baseline.buffers.push_back(attachment);
  glasswyrm::server::CompositorSnapshotSubmission::Damage damage;
  damage.surface_id = attachment.attach.surface_id;
  damage.rectangles.push_back({0, 0, 320, 200});
  baseline.damages.push_back(damage);
  require(vrr_bridge.submit_compositor(baseline, error),
          "publish synchronized buffered reconnect baseline: " + error);
  drive_until(vrr_bridge, [&] { return vrr_bridge.compositor_result_ready(); },
              "buffered reconnect baseline compositor frame timed out");
  vrr_bridge.clear_transaction_result();

  const auto interrupted_policy = glasswyrm::server::project_policy(
      *reconnect_snapshot, 22, 22, vrr_layout, vrr_cache);
  require(vrr_bridge.submit_policy(interrupted_policy, error),
          "stage interrupted buffered compositor policy");
  drive_until(vrr_bridge, [&] { return vrr_bridge.policy_result_ready(); },
              "interrupted buffered compositor policy timed out");
  const auto interrupted_snapshot = glasswyrm::server::apply_policy_result(
      *reconnect_snapshot, vrr_bridge.policy_result(), vrr_layout, vrr_cache);
  require(interrupted_snapshot && vrr_cache->generation() == 22,
          "interrupted policy stages a newer VRR generation");
  auto delta = glasswyrm::server::project_compositor(
      *interrupted_snapshot, 23, 23, true, vrr_layout, vrr_cache);
  delta.commit_id = 23;
  delta.generation = 23;
  delta.buffers.clear();
  delta.damages = baseline.damages;
  require(::kill(vrr_compositor_process, SIGSTOP) == 0,
          "suspend compositor before interrupted full frame");
  require(vrr_bridge.submit_compositor(delta, error),
          "submit attachment-omitting buffered compositor transaction");
  stop_while_suspended(vrr_compositor_process);
  drive_until(vrr_bridge, [&] { return !vrr_bridge.ready(); },
              "in-flight buffered compositor disconnect was not reported");
  vrr_compositor_process = launch_output_model_vrr(
      argv[2], vrr_compositor_socket, root + "/vrr-reconnect-dump-2");
  drive_until(vrr_bridge,
              [&] { return vrr_bridge.compositor_interrupted_ready(); },
              "reconnected buffered compositor did not expose interruption");
  require(vrr_cache->generation() == 22,
          "canonical reconnect replay restores the staged VRR generation");
  const auto rollback_policy = glasswyrm::server::project_policy(
      *reconnect_snapshot, 24, 24, vrr_layout, vrr_cache);
  require(vrr_bridge.prepare_rollback() &&
              vrr_bridge.submit_policy(rollback_policy, error),
          "interrupted forward frame begins coordinated policy rollback");
  drive_until(vrr_bridge, [&] { return vrr_bridge.policy_result_ready(); },
              "coordinated rollback policy timed out");
  const auto rollback_snapshot = glasswyrm::server::apply_policy_result(
      *reconnect_snapshot, vrr_bridge.policy_result(), vrr_layout, vrr_cache);
  require(rollback_snapshot && vrr_cache->generation() == 24,
          "coordinated rollback stages committed VRR policy");
  auto rollback_scene = glasswyrm::server::project_compositor(
      *rollback_snapshot, 25, 25, true, vrr_layout, vrr_cache);
  rollback_scene.damages = baseline.damages;
  require(::kill(vrr_compositor_process, SIGSTOP) == 0,
          "suspend compositor before rollback frame");
  require(vrr_bridge.submit_compositor(rollback_scene, error),
          "submit coordinated compositor rollback");
  stop_while_suspended(vrr_compositor_process);
  drive_until(vrr_bridge, [&] { return !vrr_bridge.ready(); },
              "in-flight compositor rollback disconnect was not reported");
  vrr_compositor_process = launch_output_model_vrr(
      argv[2], vrr_compositor_socket, root + "/vrr-reconnect-dump-3");
  drive_until(vrr_bridge,
              [&] { return vrr_bridge.compositor_interrupted_ready(); },
              "reconnected compositor rollback did not expose interruption");
  require(vrr_cache->generation() == 24,
          "rollback reconnect restores the staged VRR generation");
  auto rollback_retry = glasswyrm::server::project_compositor(
      *rollback_snapshot, 26, 26, true, vrr_layout, vrr_cache);
  rollback_retry.damages = baseline.damages;
  require(vrr_bridge.prepare_compositor_retry() &&
              vrr_bridge.submit_compositor(rollback_retry, error),
          "retry committed compositor rollback without another policy pass");
  drive_until(vrr_bridge, [&] { return vrr_bridge.compositor_result_ready(); },
              "retried committed compositor rollback timed out");
  vrr_bridge.clear_transaction_result();

  glasswyrm::server::CompositorContentSubmission interrupted_content;
  interrupted_content.commit_id = 27;
  interrupted_content.generation = 27;
  interrupted_content.damages = baseline.damages;
  require(::kill(vrr_compositor_process, SIGSTOP) == 0,
          "suspend compositor before interrupted incremental frame");
  require(vrr_bridge.submit_content(interrupted_content, error),
          "submit interrupted incremental content frame");
  stop_while_suspended(vrr_compositor_process);
  drive_until(vrr_bridge, [&] { return !vrr_bridge.ready(); },
              "in-flight content disconnect was not reported");
  vrr_compositor_process = launch_output_model_vrr(
      argv[2], vrr_compositor_socket, root + "/vrr-reconnect-dump-4");
  drive_until(vrr_bridge, [&] { return vrr_bridge.content_rejected_ready(); },
              "reconnected content frame did not request canonical replay");
  require(vrr_cache->generation() == 24,
          "content reconnect preserves the accepted VRR checkpoint");
  vrr_bridge.clear_transaction_result();

  require(vrr_cache->set_policy(changed_output, GWIPC_VRR_POLICY_FOCUSED),
          "stage focused policy for membership reconciliation");
  glasswyrm::server::LifecycleSnapshot membership_snapshot;
  membership_snapshot.root_order = {10};
  glasswyrm::server::LifecycleWindow membership_window;
  membership_window.xid = 10;
  membership_window.parent = membership_snapshot.root_window;
  membership_window.requested_width = 320;
  membership_window.requested_height = 200;
  membership_window.map_requested = true;
  membership_window.creation_serial = 1;
  membership_window.map_serial = 2;
  membership_snapshot.windows.emplace(10, membership_window);
  glasswyrm::server::VrrWindowStateStore published_vrr;
  glasswyrm::server::synchronize_vrr_windows(
      membership_snapshot, published_vrr, *vrr_cache);
  const auto first_membership_policy = glasswyrm::server::project_policy(
      membership_snapshot, 30, 30, vrr_layout, vrr_cache);
  const bool first_membership_submitted =
      vrr_bridge.submit_policy(first_membership_policy, error);
  require(first_membership_submitted,
          "submit pre-placement VRR policy transaction: " + error);
  drive_until(vrr_bridge, [&] { return vrr_bridge.policy_result_ready(); },
              "pre-placement VRR policy result timed out");
  const auto first_membership_result =
      glasswyrm::server::apply_policy_result(
          membership_snapshot, vrr_bridge.policy_result(), vrr_layout,
          vrr_cache);
  require(first_membership_result &&
              !glasswyrm::server::policy_output_facts_match(
                  membership_snapshot, *first_membership_result) &&
              !vrr_cache->windows().at(10).policy_result->eligible &&
              (vrr_cache->windows().at(10).policy_result->reason_flags &
               GWIPC_VRR_REASON_SURFACE_MEMBERSHIP_INVALID) != 0,
          "first GWM pass derives output membership without publishing a stale "
          "candidate");
  require(vrr_bridge.prepare_policy_reconciliation(),
          "policy-ready bridge permits a bounded reconciliation pass");
  const auto second_membership_policy = glasswyrm::server::project_policy(
      *first_membership_result, 31, 31, vrr_layout, vrr_cache);
  const bool second_membership_submitted =
      vrr_bridge.submit_policy(second_membership_policy, error);
  require(second_membership_submitted,
          "submit derived-membership VRR policy transaction: " + error);
  drive_until(vrr_bridge, [&] { return vrr_bridge.policy_result_ready(); },
              "derived-membership VRR policy result timed out");
  const auto stable_membership_result =
      glasswyrm::server::apply_policy_result(
          *first_membership_result, vrr_bridge.policy_result(), vrr_layout,
          vrr_cache);
  require(stable_membership_result &&
              glasswyrm::server::policy_output_facts_match(
                  *first_membership_result, *stable_membership_result) &&
              vrr_cache->windows().at(10).policy_result->eligible &&
              vrr_cache->windows().at(10).policy_result->selected &&
              (vrr_cache->windows().at(10).policy_result->reason_flags &
               GWIPC_VRR_REASON_SURFACE_MEMBERSHIP_INVALID) == 0,
          "second real GWM pass converges and selects the focused candidate "
          "before any compositor submission");
  require(vrr_bridge.prepare_rollback(),
          "clear the isolated reconciliation transaction");

  stop(vrr_compositor_process);
  stop(vrr_policy_process);
  return 0;
}
