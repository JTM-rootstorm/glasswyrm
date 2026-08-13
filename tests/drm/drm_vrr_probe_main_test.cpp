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
    std::string pattern = "/tmp/glasswyrm-vrr-probe-main-XXXXXX";
    gw::test::require(::mkdtemp(pattern.data()) != nullptr,
                      "create direct probe temporary directory");
    path_ = pattern;
  }
  ~TemporaryDirectory() { std::filesystem::remove_all(path_); }

  [[nodiscard]] std::filesystem::path file(
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
                    "create direct probe output pipe");
  const pid_t child = ::fork();
  gw::test::require(child >= 0, "fork direct probe process");
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
    gw::test::require(count == 0, "read direct probe output");
    break;
  }
  ::close(pipe_fds[0]);

  int wait_status{};
  gw::test::require(::waitpid(child, &wait_status, 0) == child &&
                        WIFEXITED(wait_status),
                    "wait for direct probe process");
  result.status = WEXITSTATUS(wait_status);
  return result;
}

std::vector<std::string> live_arguments(
    const std::filesystem::path& device,
    const std::filesystem::path& output) {
  return {"--device", device.string(),
          "--connector", "DP-1",
          "--mode", "2x2@120000",
          "--run-id", std::string(32, 'a'),
          "--output", output.string(),
          "--warmup", "0",
          "--samples", "1"};
}

void require_guarded(const std::string& executable,
                     const std::vector<std::string>& arguments,
                     const char* hardware_opt_in,
                     const std::filesystem::path& output) {
  const auto result = run(executable, arguments, hardware_opt_in);
  gw::test::require(
      result.status == 1 &&
          result.output.find("requires exactly GW_ALLOW_HARDWARE_TESTS=1") !=
              std::string::npos,
      "direct probe refuses missing or malformed hardware opt-in");
  gw::test::require(!std::filesystem::exists(output),
                    "guard fires before creating the probe report");
}

}  // namespace

int main(int argc, char** argv) {
  gw::test::require(argc == 2, "direct probe test requires the probe binary");
  const std::string executable = argv[1];
  TemporaryDirectory directory;

  const auto help = run(executable, {"--help"}, "malformed");
  gw::test::require(help.status == 0 &&
                        help.output.find("Usage: gw_drm_vrr_probe") !=
                            std::string::npos,
                    "help remains available without hardware authorization");

  const auto missing_output = directory.file("unset.jsonl");
  const auto missing_arguments = live_arguments(
      directory.file("missing-device"), missing_output);
  require_guarded(executable, missing_arguments, nullptr, missing_output);

  constexpr std::array malformed{"", "0", "true", "01", " 1", "1 "};
  for (std::size_t index = 0; index < malformed.size(); ++index) {
    const auto output = directory.file("malformed-" + std::to_string(index) +
                                       ".jsonl");
    require_guarded(executable,
                    live_arguments(directory.file("missing-device"), output),
                    malformed[index], output);
  }

  const auto authorized_output = directory.file("authorized.jsonl");
  const auto authorized = run(
      executable,
      live_arguments(directory.file("missing-device"), authorized_output),
      "1");
  gw::test::require(
      authorized.status == 1 &&
          authorized.output.find("GW_ALLOW_HARDWARE_TESTS") ==
              std::string::npos &&
          std::filesystem::is_regular_file(authorized_output),
      "exact opt-in reaches the probe after report creation");
  return 0;
}
