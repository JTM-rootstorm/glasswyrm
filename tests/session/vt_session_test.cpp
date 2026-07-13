#include "backends/session/vt_session.hpp"
#include "tests/helpers/test_support.hpp"

#include <algorithm>
#include <csignal>
#include <string>
#include <string_view>
#include <vector>

namespace {

using glasswyrm::session::DeviceIdentity;
using glasswyrm::session::DirectSessionState;
using glasswyrm::session::DirectVirtualTerminalSession;
using glasswyrm::session::DisplaySessionControl;
using glasswyrm::session::VirtualTerminalApi;
using glasswyrm::session::VirtualTerminalMode;
using glasswyrm::session::VirtualTerminalSignals;
using glasswyrm::session::VirtualTerminalState;

struct OperationLog {
  std::vector<std::string> values;
  std::string fail_on;

  bool record(std::string value) {
    values.push_back(std::move(value));
    return values.back() != fail_on;
  }
};

class FakeVirtualTerminalApi final : public VirtualTerminalApi {
public:
  explicit FakeVirtualTerminalApi(OperationLog& log) : log_(log) {}

  int open_terminal(std::string_view path) override {
    return log_.record("open:" + std::string(path)) ? 9 : -1;
  }
  bool identify(int, DeviceIdentity& value) override {
    if (!log_.record("identify")) return false;
    value = identity;
    return true;
  }
  bool get_state(int, VirtualTerminalState& value) override {
    if (!log_.record("get_state")) return false;
    value = saved_state;
    return true;
  }
  bool get_mode(int, VirtualTerminalMode& value) override {
    if (!log_.record("get_mode")) return false;
    value = saved_mode;
    return true;
  }
  bool get_kd_mode(int, int& value) override {
    if (!log_.record("get_kd_mode")) return false;
    value = saved_kd_mode;
    return true;
  }
  bool activate(int, unsigned number) override {
    return log_.record("activate:" + std::to_string(number));
  }
  bool wait_until_active(int, unsigned number) override {
    return log_.record("wait:" + std::to_string(number));
  }
  bool set_process_mode(int, int release_signal,
                        int acquire_signal) override {
    return log_.record("set_process:" + std::to_string(release_signal) + ":" +
                       std::to_string(acquire_signal));
  }
  bool set_mode(int, const VirtualTerminalMode& value) override {
    restored_mode = value;
    return log_.record("restore_vt_mode");
  }
  bool set_graphics_mode(int) override {
    return log_.record("set_graphics");
  }
  bool set_kd_mode(int, int value) override {
    restored_kd_mode = value;
    return log_.record("restore_kd:" + std::to_string(value));
  }
  bool acknowledge_release(int) override { return log_.record("ack_release"); }
  bool acknowledge_acquire(int) override { return log_.record("ack_acquire"); }
  void close_terminal(int) noexcept override { (void)log_.record("close"); }
  std::string last_error() const override { return "fake failure"; }

  DeviceIdentity identity{4, 4, true};
  VirtualTerminalState saved_state{2, 0, 0};
  VirtualTerminalMode saved_mode{0, 1, 3, 4, 5};
  int saved_kd_mode{0};
  VirtualTerminalMode restored_mode{};
  int restored_kd_mode{-1};

private:
  OperationLog& log_;
};

class FakeDisplayControl final : public DisplaySessionControl {
public:
  explicit FakeDisplayControl(OperationLog& log) : log_(log) {}

  bool quiesce_pending_flip(std::string& error) override {
    return run("quiesce", error);
  }
  bool acquire_master(std::string& error) override {
    return run("acquire_master", error);
  }
  bool drop_master(std::string& error) override {
    return run("drop_master", error);
  }
  bool present_committed_frame(std::string& error) override {
    return run("full_modeset", error);
  }
  bool restore_original_display(std::string& error) override {
    return run("restore_display", error);
  }

private:
  bool run(const std::string& operation, std::string& error) {
    if (log_.record(operation)) return true;
    error = operation + " failed";
    return false;
  }

  OperationLog& log_;
};

VirtualTerminalSignals signals() { return {SIGUSR1, SIGUSR2}; }

void require_subsequence(const std::vector<std::string>& values,
                         const std::vector<std::string>& expected,
                         const std::string& label) {
  auto position = values.begin();
  for (const auto& item : expected) {
    position = std::find(position, values.end(), item);
    gw::test::require(position != values.end(), label + ": missing " + item);
    ++position;
  }
}

void path_parsing() {
  unsigned number = 99;
  gw::test::require(
      glasswyrm::session::parse_virtual_terminal_path("/dev/tty1", number) &&
          number == 1,
      "parse first VT");
  gw::test::require(
      glasswyrm::session::parse_virtual_terminal_path("/dev/tty63", number) &&
          number == 63,
      "parse last supported VT");
  for (const std::string_view invalid :
       {"/dev/tty", "/dev/tty0", "/dev/tty01", "/dev/tty64", "/dev/ttyS0",
        "/dev/pts/4", "/tmp/tty4", "/dev/tty4x"}) {
    gw::test::require(
        !glasswyrm::session::parse_virtual_terminal_path(invalid, number),
        "reject non-VT path");
  }
}

void device_validation() {
  for (const DeviceIdentity invalid :
       {DeviceIdentity{136, 4, true}, DeviceIdentity{4, 64, true},
        DeviceIdentity{4, 4, false}}) {
    OperationLog log;
    FakeVirtualTerminalApi api(log);
    FakeDisplayControl display(log);
    api.identity = invalid;
    DirectVirtualTerminalSession session(api, display);
    std::string error;
    gw::test::require(!session.acquire("/dev/tty4", signals(), error),
                      "reject PTY, serial, or non-character device");
    gw::test::require(log.values.back() == "close",
                      "invalid device closes descriptor");
  }
}

void lifecycle_and_restore_order() {
  OperationLog log;
  FakeVirtualTerminalApi api(log);
  FakeDisplayControl display(log);
  DirectVirtualTerminalSession session(api, display);
  std::string error;
  gw::test::require(session.acquire("/dev/tty4", signals(), error),
                    "acquire direct VT");
  gw::test::require(session.state() == DirectSessionState::Active &&
                        session.previous_active_terminal() == 2,
                    "save active VT and enter active state");
  require_subsequence(log.values,
                      {"get_state", "get_mode", "get_kd_mode", "activate:4",
                       "wait:4", "set_process:" + std::to_string(SIGUSR1) +
                                     ":" + std::to_string(SIGUSR2),
                       "set_graphics", "acquire_master"},
                      "acquisition order");

  gw::test::require(session.release(error), "release active VT");
  gw::test::require(session.state() == DirectSessionState::Suspended,
                    "release enters suspended state");
  require_subsequence(log.values,
                      {"quiesce", "drop_master", "ack_release"},
                      "release order");

  gw::test::require(session.reacquire(error), "reacquire suspended VT");
  require_subsequence(log.values,
                      {"ack_acquire", "acquire_master", "full_modeset"},
                      "acquire order");

  gw::test::require(session.restore(error), "restore direct session");
  require_subsequence(log.values,
                      {"restore_display", "drop_master", "restore_kd:0",
                       "restore_vt_mode", "activate:2", "wait:2", "close"},
                      "normal restoration order");
  gw::test::require(api.restored_kd_mode == api.saved_kd_mode &&
                        api.restored_mode.release_signal ==
                            api.saved_mode.release_signal,
                    "restore saved KD and VT modes");
}

void acquisition_failures_unwind() {
  const std::vector<std::string> failures{
      "open:/dev/tty4", "identify",     "get_state",      "get_mode",
      "get_kd_mode",    "activate:4",   "wait:4",
      "set_process:" + std::to_string(SIGUSR1) + ":" +
          std::to_string(SIGUSR2),
      "set_graphics", "acquire_master"};
  for (const auto& failure : failures) {
    OperationLog log;
    log.fail_on = failure;
    FakeVirtualTerminalApi api(log);
    FakeDisplayControl display(log);
    DirectVirtualTerminalSession session(api, display);
    std::string error;
    gw::test::require(!session.acquire("/dev/tty4", signals(), error) &&
                          !error.empty(),
                      "surface every acquisition failure");
    if (failure != "open:/dev/tty4")
      gw::test::require(std::find(log.values.begin(), log.values.end(), "close") !=
                            log.values.end(),
                        "close after partial acquisition");
  }

  OperationLog log;
  log.fail_on = "acquire_master";
  FakeVirtualTerminalApi api(log);
  FakeDisplayControl display(log);
  DirectVirtualTerminalSession session(api, display);
  std::string error;
  (void)session.acquire("/dev/tty4", signals(), error);
  require_subsequence(log.values,
                      {"set_graphics", "acquire_master", "restore_kd:0",
                       "restore_vt_mode", "activate:2", "wait:2", "close"},
                      "reverse-order failed-acquire cleanup");
}

void transition_failures() {
  OperationLog log;
  FakeVirtualTerminalApi api(log);
  FakeDisplayControl display(log);
  DirectVirtualTerminalSession session(api, display);
  std::string error;
  gw::test::require(session.acquire("/dev/tty4", signals(), error),
                    "prepare transition failure");
  log.fail_on = "quiesce";
  gw::test::require(!session.release(error) &&
                        std::find(log.values.begin(), log.values.end(),
                                  "ack_release") == log.values.end(),
                    "failed quiescence prevents release acknowledgement");

  OperationLog reacquire_log;
  FakeVirtualTerminalApi reacquire_api(reacquire_log);
  FakeDisplayControl reacquire_display(reacquire_log);
  DirectVirtualTerminalSession reacquire_session(reacquire_api,
                                                 reacquire_display);
  gw::test::require(
      reacquire_session.acquire("/dev/tty4", signals(), error) &&
          reacquire_session.release(error),
      "prepare reacquire failure");
  reacquire_log.fail_on = "acquire_master";
  gw::test::require(!reacquire_session.reacquire(error),
                    "master reacquisition failure is fatal");
  require_subsequence(reacquire_log.values,
                      {"ack_acquire", "acquire_master"},
                      "acquire failure ordering");
}

} // namespace

int main() {
  path_parsing();
  device_validation();
  lifecycle_and_restore_order();
  acquisition_failures_unwind();
  transition_failures();
  return 0;
}
