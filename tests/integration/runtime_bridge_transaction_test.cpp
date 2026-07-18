#include "glasswyrmd/runtime_bridge.hpp"
#include "glasswyrmd/cursor_presenter.hpp"
#include "glasswyrmd/output_scene_projection.hpp"
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

void stop(const pid_t child) {
  if (child <= 0) return;
  (void)::kill(child, SIGTERM);
  int status = 0;
  require(::waitpid(child, &status, 0) == child, "reap bridge peer");
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
  stop(output_compositor_process);
  stop(output_policy_process);
  return 0;
}
