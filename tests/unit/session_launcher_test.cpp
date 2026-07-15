#include "session/launcher.hpp"
#include "session/process_supervisor.hpp"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

int failures = 0;
int fixture_log_fd = -1;
const char *fixture_name = nullptr;
volatile std::sig_atomic_t supervisor_signal = 0;

void require(bool condition, const char *message) {
  if (condition)
    return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}

extern "C" void fixture_signal(int signal) {
  if (fixture_log_fd >= 0 && fixture_name) {
    (void)::write(fixture_log_fd, fixture_name, std::strlen(fixture_name));
    (void)::write(fixture_log_fd, "\n", 1);
  }
  _exit(128 + signal);
}

extern "C" void supervisor_signal_handler(int signal) {
  supervisor_signal = signal;
}

std::string self_path() {
  std::vector<char> buffer(4096);
  const auto size = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
  if (size <= 0)
    return {};
  return {buffer.data(), static_cast<std::size_t>(size)};
}

int run_fixture(int argc, char **argv) {
  const std::string_view mode = argc > 2 ? argv[2] : "";
  if (mode == "exit")
    return argc > 3 ? std::atoi(argv[3]) : 0;
  if (mode == "environment")
    return argc > 3 && std::getenv("DISPLAY") &&
                   std::string_view(std::getenv("DISPLAY")) == argv[3]
               ? 0
               : 9;
  if (mode != "ready" || argc < 5)
    return 8;

  fixture_name = argv[4];
  if (argc > 5)
    fixture_log_fd = ::open(argv[5], O_WRONLY | O_CREAT | O_APPEND, 0600);
  struct sigaction action{};
  action.sa_handler = fixture_signal;
  ::sigemptyset(&action.sa_mask);
  (void)::sigaction(SIGINT, &action, nullptr);
  (void)::sigaction(SIGTERM, &action, nullptr);

  const int marker = ::open(argv[3], O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (marker < 0)
    return 10;
  (void)::close(marker);
  for (;;)
    ::pause();
}

struct TempDirectory {
  std::string path;
  TempDirectory() {
    std::string pattern = "/tmp/glasswyrm-session-test-XXXXXX";
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    if (char *created = ::mkdtemp(writable.data()))
      path = created;
  }
  ~TempDirectory() {
    if (!path.empty())
      std::filesystem::remove_all(path);
  }
};

std::vector<char *> mutable_argv(std::vector<std::string> &arguments) {
  std::vector<char *> result;
  for (auto &argument : arguments)
    result.push_back(argument.data());
  return result;
}

void test_cli_and_argv() {
  using namespace glasswyrm::session;
  std::vector<std::string> arguments = {"glasswyrm-session",
                                        "--runtime-dir",
                                        "/run/user/0/gw",
                                        "--display",
                                        "99",
                                        "--drm-device",
                                        "/dev/dri/card0",
                                        "--tty",
                                        "/dev/tty2",
                                        "--connector",
                                        "Virtual-1",
                                        "--mode",
                                        "1024x768@60000",
                                        "--input-device",
                                        "/dev/input/event4",
                                        "--input-device",
                                        "/dev/input/event5",
                                        "--xkb-layout",
                                        "us",
                                        "--drm-api",
                                        "atomic",
                                        "--mirror-dump-dir",
                                        "/tmp/mirror",
                                        "--scene-manifest",
                                        "/tmp/scene",
                                        "--drm-report",
                                        "/tmp/drm",
                                        "--x11-trace",
                                        "/tmp/x11",
                                        "--client",
                                        "xterm",
                                        "-name",
                                        "hello world",
                                        "$()"};
  auto argv = mutable_argv(arguments);
  Options options;
  std::ostringstream output;
  std::ostringstream error;
  require(parse_options(static_cast<int>(argv.size()), argv.data(), options,
                        output, error) == ParseOptionsResult::Run,
          "valid launcher CLI parses");
  RuntimePaths paths;
  std::string detail;
  require(make_runtime_paths(options, paths, detail), "runtime paths fit");
  const auto plan = build_command_plan(options, paths);
  require(plan.children.size() == 4, "launcher builds four child commands");
  require(plan.children[0].argv ==
              std::vector<std::string>(
                  {"gwm", "--ipc-socket", "/run/user/0/gw/gwm.sock"}),
          "gwm argv is exact");
  require(plan.children[1].argv.front() == "gwcomp" &&
              plan.children[1].argv[2] == "drm",
          "gwcomp argv selects DRM directly");
  require(std::find(plan.children[2].argv.begin(), plan.children[2].argv.end(),
                    "--libinput-device") != plan.children[2].argv.end(),
          "server argv includes real input devices");
  require(
      plan.children[3].argv ==
          std::vector<std::string>({"xterm", "-name", "hello world", "$()"}),
      "client argv preserves metacharacters without a shell");
  require(plan.children[3].environment ==
              std::vector<std::string>({"DISPLAY=:99"}),
          "client receives exact DISPLAY environment");
  require(plan.children[0].readiness_socket == paths.wm_socket &&
              plan.children[1].readiness_socket == paths.compositor_socket &&
              plan.children[2].readiness_socket == paths.x11_socket,
          "dependency readiness paths are ordered");
}

void test_cli_rejections() {
  using namespace glasswyrm::session;
  std::vector<std::string> missing = {"glasswyrm-session", "--runtime-dir",
                                      "relative"};
  auto missing_argv = mutable_argv(missing);
  Options options;
  std::ostringstream output;
  std::ostringstream error;
  require(parse_options(static_cast<int>(missing_argv.size()),
                        missing_argv.data(), options, output,
                        error) == ParseOptionsResult::ExitFailure,
          "incomplete CLI is rejected");

  Options long_path;
  long_path.runtime_dir = "/" + std::string(200, 'x');
  RuntimePaths paths;
  std::string detail;
  require(!make_runtime_paths(long_path, paths, detail),
          "overlong Unix socket paths are rejected");
}

glasswyrm::session::ChildSpec ready_child(const std::string &self,
                                          const std::string &socket,
                                          const std::string &name,
                                          const std::string &log = {}) {
  glasswyrm::session::ChildSpec child;
  child.name = name;
  child.argv = {self, "--fixture", "ready", socket, name};
  if (!log.empty())
    child.argv.push_back(log);
  child.readiness_socket = socket;
  child.readiness_requires_socket = false;
  return child;
}

void test_supervisor_readiness_signal_and_reverse_shutdown() {
  using namespace glasswyrm::session;
  TempDirectory temp;
  require(!temp.path.empty(), "temporary directory created");
  const std::string log = temp.path + "/shutdown.log";
  std::vector<ChildSpec> children;
  const auto self = self_path();
  children.push_back(ready_child(self, temp.path + "/gwm.sock", "gwm", log));
  children.push_back(
      ready_child(self, temp.path + "/gwcomp.sock", "gwcomp", log));
  children.push_back(
      ready_child(self, temp.path + "/server.sock", "glasswyrmd", log));

  struct sigaction action{};
  action.sa_handler = supervisor_signal_handler;
  ::sigemptyset(&action.sa_mask);
  struct sigaction old_action{};
  (void)::sigaction(SIGTERM, &action, &old_action);
  supervisor_signal = 0;
  std::thread trigger([] {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    (void)::kill(::getpid(), SIGTERM);
  });
  std::ostringstream error;
  ProcessSupervisor supervisor({std::chrono::milliseconds(1000),
                                std::chrono::milliseconds(300),
                                std::chrono::milliseconds(5)});
  const int result = supervisor.run(children, error, &supervisor_signal);
  trigger.join();
  (void)::sigaction(SIGTERM, &old_action, nullptr);
  if (result != 128 + SIGTERM)
    std::cerr << "signal run: " << result << " " << error.str();
  require(result == 128 + SIGTERM, "launcher reports forwarded termination");
  std::ifstream input(log);
  std::vector<std::string> order;
  for (std::string line; std::getline(input, line);)
    order.push_back(line);
  require(order == std::vector<std::string>({"glasswyrmd", "gwcomp", "gwm"}),
          "children terminate in reverse dependency order");
  errno = 0;
  require(::waitpid(-1, nullptr, WNOHANG) == -1 && errno == ECHILD,
          "supervisor leaves no zombie children");
}

void test_child_failure_and_timeout() {
  using namespace glasswyrm::session;
  const auto self = self_path();
  std::ostringstream failure_error;
  ProcessSupervisor failure_supervisor({std::chrono::milliseconds(200),
                                        std::chrono::milliseconds(100),
                                        std::chrono::milliseconds(5)});
  ChildSpec failure{"fixture", {self, "--fixture", "exit", "7"},
                    {},        std::nullopt,
                    true,      true};
  require(failure_supervisor.run({failure}, failure_error) == 1 &&
              failure_error.str().find("required process fixture exited") !=
                  std::string::npos,
          "required child failure is reported");

  TempDirectory temp;
  std::ostringstream timeout_error;
  ProcessSupervisor timeout_supervisor({std::chrono::milliseconds(50),
                                        std::chrono::milliseconds(100),
                                        std::chrono::milliseconds(5)});
  ChildSpec timeout{
      "timeout",
      {self, "--fixture", "ready", temp.path + "/actual.sock", "timeout"},
      {},
      temp.path + "/never.sock",
      false,
      true};
  const int timeout_result = timeout_supervisor.run({timeout}, timeout_error);
  if (timeout_result != 1 ||
      timeout_error.str().find("timed out") == std::string::npos)
    std::cerr << "timeout run: " << timeout_result << " "
              << timeout_error.str();
  require(timeout_result == 1 &&
              timeout_error.str().find("timed out") != std::string::npos,
          "readiness timeout is bounded and reported");
}

void test_optional_client_and_environment() {
  using namespace glasswyrm::session;
  TempDirectory temp;
  const auto self = self_path();
  ChildSpec service = ready_child(self, temp.path + "/service.sock", "service");
  ChildSpec client{"client",
                   {self, "--fixture", "environment", ":47"},
                   {"DISPLAY=:47"},
                   std::nullopt,
                   true,
                   false};
  std::ostringstream error;
  ProcessSupervisor supervisor({std::chrono::milliseconds(500),
                                std::chrono::milliseconds(100),
                                std::chrono::milliseconds(5)});
  const int result = supervisor.run({service, client}, error);
  if (result != 0)
    std::cerr << "optional run: " << result << " " << error.str();
  require(result == 0,
          "successful optional client ends a clean supervised session");
}

} // namespace

int main(int argc, char **argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--fixture")
    return run_fixture(argc, argv);
  test_cli_and_argv();
  test_cli_rejections();
  test_supervisor_readiness_signal_and_reverse_shutdown();
  test_child_failure_and_timeout();
  test_optional_client_and_environment();
  if (failures != 0)
    std::cerr << failures << " session launcher test(s) failed\n";
  return failures == 0 ? 0 : 1;
}
