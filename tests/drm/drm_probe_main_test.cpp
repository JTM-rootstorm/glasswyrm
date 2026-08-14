#include "tests/helpers/test_support.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

struct RunResult {
  int status{};
  std::string output;
};

RunResult run(const std::string& executable,
              const std::vector<std::string>& arguments,
              const char* hardware_opt_in) {
  std::array<int, 2> pipe_fds{};
  gw::test::require(::pipe2(pipe_fds.data(), O_CLOEXEC) == 0,
                    "create DRM probe output pipe");
  const pid_t child = ::fork();
  gw::test::require(child >= 0, "fork DRM probe process");
  if (child == 0) {
    (void)::close(pipe_fds[0]);
    if (::dup2(pipe_fds[1], STDOUT_FILENO) < 0 ||
        ::dup2(pipe_fds[1], STDERR_FILENO) < 0)
      _exit(126);
    (void)::close(pipe_fds[1]);
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

  (void)::close(pipe_fds[1]);
  RunResult result;
  std::array<char, 4096> buffer{};
  for (;;) {
    const auto count = ::read(pipe_fds[0], buffer.data(), buffer.size());
    if (count > 0) {
      result.output.append(buffer.data(), static_cast<std::size_t>(count));
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    gw::test::require(count == 0, "read DRM probe output");
    break;
  }
  (void)::close(pipe_fds[0]);
  int wait_status{};
  gw::test::require(::waitpid(child, &wait_status, 0) == child &&
                        WIFEXITED(wait_status),
                    "wait for DRM probe process");
  result.status = WEXITSTATUS(wait_status);
  return result;
}

std::vector<std::string> probe_arguments(
    const std::filesystem::path& output) {
  return {"--device", "/definitely/missing/drm-card", "--output",
          output.string()};
}

}  // namespace

int main(int argc, char** argv) {
  gw::test::require(argc == 2, "DRM probe test requires the probe binary");
  const std::string executable = argv[1];
  const auto output = std::filesystem::temp_directory_path() /
                      ("glasswyrm-drm-probe-main-" +
                       std::to_string(static_cast<long long>(::getpid())) +
                       ".json");
  std::filesystem::remove(output);

  const auto help = run(executable, {"--help"}, "malformed");
  gw::test::require(help.status == 0 &&
                        help.output.find("Usage: gw_drm_probe") !=
                            std::string::npos,
                    "help remains available without hardware authorization");

  constexpr std::array<const char*, 7> malformed{
      nullptr, "", "0", "true", "01", " 1", "1 "};
  for (const auto* value : malformed) {
    const auto guarded = run(executable, probe_arguments(output), value);
    gw::test::require(
        guarded.status == 1 &&
            guarded.output.find(
                "requires exactly GW_ALLOW_HARDWARE_TESTS=1") !=
                std::string::npos &&
            !std::filesystem::exists(output),
        "missing or malformed hardware opt-in is rejected before DRM access");
  }

  const auto authorized = run(executable, probe_arguments(output), "1");
  gw::test::require(
      authorized.status == 1 &&
          authorized.output.find("GW_ALLOW_HARDWARE_TESTS") ==
              std::string::npos &&
          !std::filesystem::exists(output),
      "exact hardware opt-in reaches real DRM path handling");
  return 0;
}
