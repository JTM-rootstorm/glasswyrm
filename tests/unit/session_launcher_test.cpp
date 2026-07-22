#include "session/launcher.hpp"
#include "session/process_supervisor.hpp"

#include "config.hpp"

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
    const auto name_written =
        ::write(fixture_log_fd, fixture_name, std::strlen(fixture_name));
    const auto newline_written = ::write(fixture_log_fd, "\n", 1);
    static_cast<void>(name_written);
    static_cast<void>(newline_written);
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
  if (mode == "empty-marker") {
    struct stat status{};
    return argc > 3 && ::stat(argv[3], &status) == 0 && status.st_size == 0
               ? 0
               : 9;
  }
  if ((mode != "ready" && mode != "replace-ready" && mode != "ready-two") ||
      argc < 5)
    return 8;

  const bool two_paths = mode == "ready-two";
  const int name_index = two_paths ? 5 : 4;
  if (argc <= name_index)
    return 8;
  if (mode == "replace-ready") {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (::unlink(argv[3]) != 0)
      return 11;
  }

  fixture_name = argv[name_index];
  if (argc > name_index + 1)
    fixture_log_fd =
        ::open(argv[name_index + 1], O_WRONLY | O_CREAT | O_APPEND, 0600);
  struct sigaction action{};
  action.sa_handler = fixture_signal;
  ::sigemptyset(&action.sa_mask);
  (void)::sigaction(SIGINT, &action, nullptr);
  (void)::sigaction(SIGTERM, &action, nullptr);

  const int marker = ::open(argv[3], O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (marker < 0)
    return 10;
  (void)::close(marker);
  if (two_paths) {
    const int second = ::open(argv[4], O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (second < 0)
      return 12;
    (void)::close(second);
  }
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
                                        "/tmp/x11"};
#if GW_HAS_EXPERIMENTAL
  arguments.insert(arguments.end(),
                   {"--game-compat", "--disable-extension", "MIT-SHM"});
#endif
  arguments.insert(arguments.end(),
                   {"--renderer", "auto", "--renderer-report",
                    "/tmp/renderer", "--client", "xterm", "-name",
                    "hello world", "$()"});
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
              plan.children[1].argv[2] == "drm" &&
              std::find(plan.children[1].argv.begin(),
                        plan.children[1].argv.end(), "auto") !=
                  plan.children[1].argv.end() &&
              std::find(plan.children[1].argv.begin(),
                        plan.children[1].argv.end(), "/tmp/renderer") !=
                  plan.children[1].argv.end(),
          "gwcomp argv selects DRM and the requested renderer directly");
  require(std::find(plan.children[2].argv.begin(), plan.children[2].argv.end(),
                    "--libinput-device") != plan.children[2].argv.end(),
          "server argv includes real input devices");
#if GW_HAS_EXPERIMENTAL
  require(std::find(plan.children[2].argv.begin(), plan.children[2].argv.end(),
                    "--game-compat") != plan.children[2].argv.end() &&
              std::find(plan.children[2].argv.begin(),
                        plan.children[2].argv.end(), "MIT-SHM") !=
                  plan.children[2].argv.end(),
          "server argv carries the game profile and extension override");
#else
  require(std::find(plan.children[2].argv.begin(), plan.children[2].argv.end(),
                    "--game-compat") == plan.children[2].argv.end(),
          "historical server argv omits the game profile");
#endif
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

  std::vector<std::string> disabled_without_profile = {
      "glasswyrm-session", "--runtime-dir", "/tmp/gw", "--display", "99",
      "--drm-device", "/dev/dri/card0", "--tty", "/dev/tty2",
      "--connector", "Virtual-1", "--mode", "1024x768", "--input-device",
      "/dev/input/event0", "--disable-extension", "MIT-SHM"};
  auto disabled_argv = mutable_argv(disabled_without_profile);
  Options disabled_options;
  std::ostringstream disabled_error;
  require(parse_options(static_cast<int>(disabled_argv.size()),
                        disabled_argv.data(), disabled_options, output,
                        disabled_error) == ParseOptionsResult::ExitFailure &&
              disabled_error.str().find("requires --game-compat") !=
                  std::string::npos,
          "extension overrides require the game profile");

#if !GW_HAS_EXPERIMENTAL
  std::vector<std::string> unavailable_profile = {
      "glasswyrm-session", "--runtime-dir", "/tmp/gw", "--display", "99",
      "--drm-device", "/dev/dri/card0", "--tty", "/dev/tty2",
      "--connector", "Virtual-1", "--mode", "1024x768", "--input-device",
      "/dev/input/event0", "--game-compat"};
  auto unavailable_argv = mutable_argv(unavailable_profile);
  Options unavailable_options;
  std::ostringstream unavailable_error;
  require(parse_options(static_cast<int>(unavailable_argv.size()),
                        unavailable_argv.data(), unavailable_options, output,
                        unavailable_error) == ParseOptionsResult::ExitFailure &&
              unavailable_error.str().find("unavailable") !=
                  std::string::npos,
          "historical build rejects the unavailable game profile");
#endif
}

void test_headless_output_model_cli() {
  using namespace glasswyrm::session;
  std::vector<std::string> arguments = {
      "glasswyrm-session", "--runtime-dir", "/run/user/0/gw-headless",
      "--display", "98", "--backend", "headless", "--headless-output",
      "LEFT:800x600@60000", "--headless-output", "RIGHT:640x480@75000",
      "--headless-vrr", "LEFT=40000-60000", "--headless-vrr",
      "RIGHT=48000-75000", "--vrr-report", "/tmp/vrr-report.jsonl",
      "--output-model"};
#if GW_HAS_EXPERIMENTAL
  arguments.emplace_back("--scale-protocol");
  arguments.emplace_back("--vrr-protocol");
#endif
  auto argv = mutable_argv(arguments);
  Options options;
  std::ostringstream output;
  std::ostringstream error;
  require(parse_options(static_cast<int>(argv.size()), argv.data(), options,
                        output, error) == ParseOptionsResult::Run,
          "headless output-model CLI parses");
  RuntimePaths paths;
  std::string detail;
  require(make_runtime_paths(options, paths, detail) && paths.control_socket ==
              "/run/user/0/gw-headless/control.sock",
          "output-model session generates a private control socket");
  const auto plan = build_command_plan(options, paths);
  require(plan.children.size() == 3 &&
              plan.children[1].argv ==
                  std::vector<std::string>(
                      {"gwcomp", "--backend", "headless", "--ipc-socket",
                       "/run/user/0/gw-headless/gwcomp.sock", "--dump-dir",
                       "/run/user/0/gw-headless/frames", "--headless-output",
                       "LEFT:800x600@60000", "--headless-output",
                       "RIGHT:640x480@75000", "--headless-vrr",
                       "LEFT=40000-60000", "--headless-vrr",
                       "RIGHT=48000-75000", "--renderer", "software",
                       "--vrr-report", "/tmp/vrr-report.jsonl"}),
          "headless compositor argv is deterministic and carries all outputs");
  require(std::find(plan.children[2].argv.begin(),
                    plan.children[2].argv.end(), "--output-model") !=
              plan.children[2].argv.end() &&
              plan.children[2].additional_readiness_sockets ==
                  std::vector<std::string>(
                      {"/run/user/0/gw-headless/control.sock"}),
          "server argv and readiness include the output-control listener");
#if GW_HAS_EXPERIMENTAL
  require(std::find(plan.children[2].argv.begin(),
                    plan.children[2].argv.end(), "--scale-protocol") !=
              plan.children[2].argv.end(),
          "experimental session enables GW_SCALE explicitly");
  require(std::find(plan.children[2].argv.begin(),
                    plan.children[2].argv.end(), "--vrr-protocol") !=
              plan.children[2].argv.end(),
          "experimental session enables GW_VRR explicitly");
#endif

  std::vector<std::string> forbidden = {
      "glasswyrm-session", "--runtime-dir", "/tmp/gw", "--display", "90",
      "--backend", "headless", "--drm-device", "/dev/dri/card0"};
  auto forbidden_argv = mutable_argv(forbidden);
  Options forbidden_options;
  std::ostringstream forbidden_error;
  require(parse_options(static_cast<int>(forbidden_argv.size()),
                        forbidden_argv.data(), forbidden_options, output,
                        forbidden_error) == ParseOptionsResult::ExitFailure &&
              forbidden_error.str().find("forbids DRM") != std::string::npos,
          "headless session rejects DRM-only options");

  std::vector<std::string> drm_vrr_simulation = {
      "glasswyrm-session", "--runtime-dir", "/tmp/gw", "--display", "90",
      "--drm-device", "/dev/dri/card0", "--tty", "/dev/tty2",
      "--connector", "Virtual-1", "--mode", "1024x768", "--input-device",
      "/dev/input/event0", "--headless-vrr", "Virtual-1=40000-60000"};
  auto drm_vrr_argv = mutable_argv(drm_vrr_simulation);
  Options drm_vrr_options;
  std::ostringstream drm_vrr_error;
  require(parse_options(static_cast<int>(drm_vrr_argv.size()),
                        drm_vrr_argv.data(), drm_vrr_options, output,
                        drm_vrr_error) == ParseOptionsResult::ExitFailure &&
              drm_vrr_error.str().find("headless") != std::string::npos,
          "DRM session rejects headless VRR simulation");

  std::vector<std::string> orphan_control = {
      "glasswyrm-session", "--runtime-dir", "/tmp/gw", "--display", "90",
      "--backend", "headless", "--control-socket", "/tmp/control.sock"};
  auto orphan_argv = mutable_argv(orphan_control);
  Options orphan_options;
  std::ostringstream orphan_error;
  require(parse_options(static_cast<int>(orphan_argv.size()),
                        orphan_argv.data(), orphan_options, output,
                        orphan_error) == ParseOptionsResult::ExitFailure &&
              orphan_error.str().find("requires --output-model") !=
                  std::string::npos,
          "control socket requires the output model");

#if GW_HAS_EXPERIMENTAL
  std::vector<std::string> orphan_vrr = {
      "glasswyrm-session", "--runtime-dir", "/tmp/gw", "--display", "90",
      "--backend", "headless", "--vrr-protocol"};
  auto orphan_vrr_argv = mutable_argv(orphan_vrr);
  Options orphan_vrr_options;
  std::ostringstream orphan_vrr_error;
  require(parse_options(static_cast<int>(orphan_vrr_argv.size()),
                        orphan_vrr_argv.data(), orphan_vrr_options, output,
                        orphan_vrr_error) == ParseOptionsResult::ExitFailure &&
              orphan_vrr_error.str().find("requires --output-model") !=
                  std::string::npos,
          "VRR protocol requires the output model");
#endif
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
                    true,      true, {}, true};
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
      true,
      {},
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

void test_stale_readiness_path_is_not_accepted() {
  using namespace glasswyrm::session;
  TempDirectory temp;
  const auto self = self_path();
  const std::string marker = temp.path + "/server.sock";
  {
    std::ofstream stale(marker);
    stale << "stale";
  }

  ChildSpec service = ready_child(self, marker, "service");
  service.argv[2] = "replace-ready";
  ChildSpec client{"client",
                   {self, "--fixture", "empty-marker", marker},
                   {},
                   std::nullopt,
                   true,
                   false,
                   {},
                   true};
  std::ostringstream error;
  ProcessSupervisor supervisor({std::chrono::milliseconds(500),
                                std::chrono::milliseconds(100),
                                std::chrono::milliseconds(5)});
  const int result = supervisor.run({service, client}, error);
  if (result != 0)
    std::cerr << "stale readiness run: " << result << " " << error.str();
  require(result == 0,
          "pre-existing readiness path waits for the child's replacement");
}

void test_additional_readiness_path() {
  using namespace glasswyrm::session;
  TempDirectory temp;
  const auto self = self_path();
  const auto primary = temp.path + "/x11.sock";
  const auto control = temp.path + "/control.sock";
  ChildSpec service = ready_child(self, primary, "service");
  service.argv = {self, "--fixture", "ready-two", primary, control,
                  "service"};
  service.additional_readiness_sockets.push_back(control);
  service.additional_readiness_requires_socket = false;
  ChildSpec client{"client", {self, "--fixture", "exit", "0"}, {},
                   std::nullopt, true, false, {}, true};
  std::ostringstream error;
  ProcessSupervisor supervisor({std::chrono::milliseconds(500),
                                std::chrono::milliseconds(100),
                                std::chrono::milliseconds(5)});
  require(supervisor.run({service, client}, error) == 0,
          "supervisor waits for every declared readiness socket");
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
                   false,
                   {},
                   true};
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
  test_headless_output_model_cli();
  test_supervisor_readiness_signal_and_reverse_shutdown();
  test_child_failure_and_timeout();
  test_stale_readiness_path_is_not_accepted();
  test_additional_readiness_path();
  test_optional_client_and_environment();
  if (failures != 0)
    std::cerr << failures << " session launcher test(s) failed\n";
  return failures == 0 ? 0 : 1;
}
