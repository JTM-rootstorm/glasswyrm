#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

struct Scenario {
  const char* name;
  unsigned accepted_frames;
};

[[noreturn]] void fail(const char* scenario, const char* message) {
  std::fprintf(stderr, "gwcomp scenario matrix (%s): %s\n", scenario, message);
  std::exit(1);
}

void require(bool condition, const char* scenario, const char* message) {
  if (!condition) fail(scenario, message);
}

bool wait_for(pid_t child, int& status) {
  for (int attempt = 0; attempt < 500; ++attempt) {
    const pid_t result = ::waitpid(child, &status, WNOHANG);
    if (result == child) return true;
    if (result < 0) return false;
    (void)::usleep(10'000);
  }
  return false;
}

std::size_t manifest_lines(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::size_t count = 0;
  std::string line;
  while (std::getline(input, line)) ++count;
  return count;
}

void run(const Scenario& scenario, const char* compositor_path,
         const char* producer_path, const std::filesystem::path& suite_root) {
  const auto root = suite_root / scenario.name;
  const auto socket = (root / "gwcomp.sock").string();
  const auto dumps = (root / "dumps").string();
  std::filesystem::create_directories(root);

  const pid_t compositor = ::fork();
  require(compositor >= 0, scenario.name, "fork gwcomp");
  if (compositor == 0) {
    if (scenario.accepted_frames == 0) {
      ::execl(compositor_path, compositor_path, "--ipc-socket", socket.c_str(),
              "--dump-dir", dumps.c_str(), nullptr);
    } else {
      const std::string frames = std::to_string(scenario.accepted_frames);
      ::execl(compositor_path, compositor_path, "--ipc-socket", socket.c_str(),
              "--dump-dir", dumps.c_str(), "--max-frames", frames.c_str(),
              nullptr);
    }
    _exit(127);
  }

  struct stat status {};
  bool ready = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (::lstat(socket.c_str(), &status) == 0 && S_ISSOCK(status.st_mode)) {
      ready = true;
      break;
    }
    int child_status = 0;
    require(::waitpid(compositor, &child_status, WNOHANG) == 0, scenario.name,
            "gwcomp remains alive during startup");
    (void)::usleep(10'000);
  }
  require(ready, scenario.name, "gwcomp listener becomes ready");

  const pid_t producer = ::fork();
  require(producer >= 0, scenario.name, "fork producer");
  if (producer == 0) {
    ::execl(producer_path, producer_path, "--socket", socket.c_str(),
            "--scenario", scenario.name, nullptr);
    _exit(127);
  }
  int producer_status = 0;
  if (!wait_for(producer, producer_status)) {
    (void)::kill(producer, SIGKILL);
    (void)::waitpid(producer, nullptr, 0);
    (void)::kill(compositor, SIGKILL);
    (void)::waitpid(compositor, nullptr, 0);
    fail(scenario.name, "producer exits within timeout");
  }
  require(WIFEXITED(producer_status) && WEXITSTATUS(producer_status) == 0,
          scenario.name, "producer verifies expected acknowledgements and releases");

  if (scenario.accepted_frames == 0)
    require(::kill(compositor, SIGTERM) == 0, scenario.name,
            "stop compositor after rejection-only scenario");
  int compositor_status = 0;
  if (!wait_for(compositor, compositor_status)) {
    (void)::kill(compositor, SIGKILL);
    (void)::waitpid(compositor, nullptr, 0);
    fail(scenario.name, "compositor exits within timeout");
  }
  require(WIFEXITED(compositor_status) && WEXITSTATUS(compositor_status) == 0,
          scenario.name, "compositor exits successfully");
  require(manifest_lines(root / "dumps/frames.jsonl") == scenario.accepted_frames,
          scenario.name, "manifest contains the expected accepted frame count");
}

} // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr,
                 "usage: gwcomp_scenario_matrix_test /path/to/gwcomp "
                 "/path/to/producer\n");
    return 2;
  }
  constexpr std::array scenarios{
      Scenario{"damage-update", 2}, Scenario{"stacking", 2},
      Scenario{"visibility", 3}, Scenario{"clipping", 3},
      Scenario{"opacity", 5}, Scenario{"buffer-replace", 2},
      Scenario{"detach-remove", 2}, Scenario{"invalid-metadata", 0},
      Scenario{"invalid-buffer", 0}, Scenario{"unknown-reference", 1},
      Scenario{"snapshot-reconnect", 2}};
  char temporary[] = "/tmp/gwcomp-scenario-matrix-XXXXXX";
  if (::mkdtemp(temporary) == nullptr) {
    std::perror("gwcomp scenario matrix: mkdtemp");
    return 1;
  }
  const std::filesystem::path root = temporary;
  for (const auto& scenario : scenarios) run(scenario, argv[1], argv[2], root);
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return 0;
}
