#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <signal.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

struct Scenario { const char* name; int commits; };
constexpr std::array<Scenario, 15> kScenarios{{
    {"basic", 1}, {"snapshot-order", 1}, {"transient", 1},
    {"override-redirect", 1}, {"focus", 1}, {"stacking", 1},
    {"fullscreen", 1}, {"maximize-minimize", 1},
    {"incremental-update", 2}, {"invalid-context", 2},
    {"invalid-window", 2}, {"unknown-reference", 2},
    {"transient-cycle", 2}, {"snapshot-abort", 2},
    {"snapshot-reconnect", 2},
}};

[[noreturn]] void fail(const std::string& message) {
  std::fprintf(stderr, "gwm scenario matrix: %s\n", message.c_str());
  std::exit(1);
}
void require(bool condition, const std::string& message) {
  if (!condition) fail(message);
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  require(input.good(), "open " + path.string());
  return {std::istreambuf_iterator<char>(input), {}};
}

std::string policy_hash(const std::string& json) {
  constexpr std::string_view prefix = "\"policy_hash\": \"";
  const auto start = json.find(prefix);
  require(start != std::string::npos, "JSON contains policy hash");
  const auto value = start + prefix.size();
  const auto end = json.find('"', value);
  require(end != std::string::npos, "policy hash is terminated");
  return json.substr(value, end - value);
}

void wait_ready(pid_t compositor, const std::filesystem::path& socket) {
  struct stat status {};
  for (int attempt = 0; attempt < 300; ++attempt) {
    if (::lstat(socket.c_str(), &status) == 0 && S_ISSOCK(status.st_mode)) return;
    int child_status = 0;
    require(::waitpid(compositor, &child_status, WNOHANG) == 0,
            "gwm remains alive while listener starts");
    (void)::usleep(10'000);
  }
  fail("gwm listener readiness timeout");
}

int wait_bounded(pid_t child, std::string_view label) {
  int status = 0;
  for (int attempt = 0; attempt < 3000; ++attempt) {
    const auto result = ::waitpid(child, &status, WNOHANG);
    if (result == child) return status;
    if (result < 0) fail(std::string(label) + " waitpid failed");
    (void)::usleep(10'000);
  }
  std::fprintf(stderr, "gwm scenario matrix: timeout waiting for %.*s pid=%d\n",
               static_cast<int>(label.size()), label.data(), child);
  (void)::kill(child, SIGKILL);
  (void)::waitpid(child, &status, 0);
  fail(std::string(label) + " exceeded 30-second lifecycle deadline");
}

void run_scenario(const char* gwm, const char* producer,
                  const std::filesystem::path& root,
                  const std::filesystem::path& fixtures,
                  const Scenario& scenario) {
  const auto socket = root / (std::string(scenario.name) + ".sock");
  const auto output = root / (std::string(scenario.name) + ".json");
  const auto commits = std::to_string(scenario.commits);
  const pid_t manager = ::fork();
  require(manager >= 0, "fork gwm");
  if (manager == 0) {
    ::execl(gwm, gwm, "--ipc-socket", socket.c_str(), "--max-commits",
            commits.c_str(), nullptr);
    _exit(127);
  }
  wait_ready(manager, socket);
  const pid_t client = ::fork();
  require(client >= 0, "fork producer");
  if (client == 0) {
    ::execl(producer, producer, "--socket", socket.c_str(), "--scenario",
            scenario.name, "--output", output.c_str(), nullptr);
    _exit(127);
  }
  int status = wait_bounded(client, std::string(scenario.name) + " producer");
  require(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          std::string(scenario.name) + " producer succeeds");
  status = wait_bounded(manager, std::string(scenario.name) + " gwm");
  require(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          std::string(scenario.name) + " gwm succeeds");
  require(read_file(output) == read_file(fixtures / (std::string(scenario.name) + ".json")),
          std::string(scenario.name) + " JSON matches reviewed fixture exactly");
}

void verify_manifest(const std::filesystem::path& fixtures) {
  const pid_t verifier = ::fork();
  require(verifier >= 0, "fork sha256 verifier");
  if (verifier == 0) {
    if (::chdir(fixtures.c_str()) != 0) _exit(126);
    ::execlp("sha256sum", "sha256sum", "--check", "--strict", "SHA256SUMS", nullptr);
    _exit(127);
  }
  int status = 0;
  require(::waitpid(verifier, &status, 0) == verifier && WIFEXITED(status) &&
              WEXITSTATUS(status) == 0,
          "SHA256SUMS validates all reviewed fixtures");
}

}  // namespace

int main(int argc, char** argv) {
  require(argc == 4,
          "usage: gwm_scenario_matrix_test /path/to/gwm /path/to/producer /fixtures");
  const std::filesystem::path fixtures = argv[3];
  verify_manifest(fixtures);
  char temporary[] = "/tmp/gwm-scenario-matrix-XXXXXX";
  require(::mkdtemp(temporary) != nullptr, "create temporary directory");
  const std::filesystem::path root = temporary;
  for (const auto& scenario : kScenarios)
    run_scenario(argv[1], argv[2], root, fixtures, scenario);
  const auto basic = read_file(root / "basic.json");
  const auto reordered = read_file(root / "snapshot-order.json");
  require(policy_hash(basic) == policy_hash(reordered),
          "basic and reverse snapshot order have identical policy hashes");
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return 0;
}
