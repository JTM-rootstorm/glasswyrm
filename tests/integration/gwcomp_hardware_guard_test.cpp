#include "tests/helpers/test_support.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

struct RunResult {
  int status{};
  std::string output;
};

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    std::string pattern = "/tmp/glasswyrm-gwcomp-guard-XXXXXX";
    gw::test::require(::mkdtemp(pattern.data()) != nullptr,
                      "create gwcomp guard temporary directory");
    path_ = pattern;
  }

  ~TemporaryDirectory() { std::filesystem::remove_all(path_); }

  [[nodiscard]] std::filesystem::path path(
      const std::string_view name) const {
    return path_ / name;
  }

 private:
  std::filesystem::path path_;
};

RunResult run(const std::string& executable,
              const std::vector<std::string>& arguments,
              const char* hardware_opt_in) {
  std::array<int, 2> pipe_fds{};
  gw::test::require(::pipe2(pipe_fds.data(), O_CLOEXEC) == 0,
                    "create gwcomp output pipe");
  const pid_t child = ::fork();
  gw::test::require(child >= 0, "fork gwcomp process");
  if (child == 0) {
    ::close(pipe_fds[0]);
    if (::dup2(pipe_fds[1], STDOUT_FILENO) < 0 ||
        ::dup2(pipe_fds[1], STDERR_FILENO) < 0)
      _exit(126);
    ::close(pipe_fds[1]);
    if (hardware_opt_in == nullptr)
      ::unsetenv("GW_ALLOW_HARDWARE_TESTS");
    else
      ::setenv("GW_ALLOW_HARDWARE_TESTS", hardware_opt_in, 1);

    std::vector<char*> argv;
    argv.reserve(arguments.size() + 2);
    argv.push_back(const_cast<char*>(executable.c_str()));
    for (const auto& argument : arguments)
      argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    ::execv(executable.c_str(), argv.data());
    _exit(errno == ENOENT ? 127 : 126);
  }

  ::close(pipe_fds[1]);
  RunResult result;
  std::array<char, 4096> buffer{};
  for (;;) {
    const auto count = ::read(pipe_fds[0], buffer.data(), buffer.size());
    if (count > 0) {
      result.output.append(buffer.data(), static_cast<std::size_t>(count));
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    gw::test::require(count == 0, "read gwcomp output");
    break;
  }
  ::close(pipe_fds[0]);

  int wait_status{};
  gw::test::require(::waitpid(child, &wait_status, 0) == child &&
                        WIFEXITED(wait_status),
                    "wait for gwcomp process");
  result.status = WEXITSTATUS(wait_status);
  return result;
}

std::vector<std::string> drm_arguments(const TemporaryDirectory& directory) {
  return {"--backend",
          "drm",
          "--ipc-socket",
          directory.path("gwcomp.sock").string(),
          "--drm-device",
          directory.path("missing-card").string(),
          "--tty",
          directory.path("missing-tty").string(),
          "--mirror-dump-dir",
          directory.path("mirror").string(),
          "--drm-report",
          directory.path("drm.jsonl").string(),
          "--vrr-report",
          directory.path("vrr.jsonl").string(),
          "--renderer-report",
          directory.path("renderer.jsonl").string()};
}

void require_guarded(const std::string& executable,
                     const TemporaryDirectory& directory,
                     const char* hardware_opt_in) {
  const auto result = run(executable, drm_arguments(directory), hardware_opt_in);
  gw::test::require(
      result.status == 2 &&
          result.output.find("requires exactly GW_ALLOW_HARDWARE_TESTS=1") !=
              std::string::npos,
      "gwcomp refuses missing or malformed hardware opt-in");
  for (const auto* artifact : {"gwcomp.sock", "mirror", "drm.jsonl",
                               "vrr.jsonl", "renderer.jsonl"}) {
    gw::test::require(!std::filesystem::exists(directory.path(artifact)),
                      "guard fires before runtime artifact access");
  }
}

}  // namespace

int main(int argc, char** argv) {
  gw::test::require(argc == 2, "gwcomp guard test requires the gwcomp binary");
  const std::string executable = argv[1];

  const auto help = run(executable, {"--help"}, "malformed");
  gw::test::require(help.status == 0 &&
                        help.output.find("Usage: gwcomp") != std::string::npos,
                    "help remains available without hardware authorization");
  const auto version = run(executable, {"--version"}, "malformed");
  gw::test::require(version.status == 0 &&
                        version.output.find("gwcomp ") != std::string::npos,
                    "version remains available without hardware authorization");

  TemporaryDirectory directory;
  require_guarded(executable, directory, nullptr);
  constexpr std::array malformed{"", "0", "true", "01", " 1", "1 "};
  for (const auto* value : malformed)
    require_guarded(executable, directory, value);
  return 0;
}
