#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
struct Options {
  std::string expected_version, evidence, result;
  unsigned timeout_ms = 5000;
  int command_index = 0;
};

bool parse(int argc, char** argv, Options& out) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--") { out.command_index = i + 1; return out.command_index < argc; }
    auto value = [&](std::string& target) {
      if (++i >= argc || argv[i][0] == '\0') return false;
      target = argv[i]; return true;
    };
    if (arg == "--expected-version") { if (!value(out.expected_version)) return false; }
    else if (arg == "--evidence") { if (!value(out.evidence)) return false; }
    else if (arg == "--result") { if (!value(out.result)) return false; }
    else if (arg == "--timeout-ms") {
      std::string text; if (!value(text)) return false;
      char* end{}; const auto parsed = std::strtoul(text.c_str(), &end, 10);
      if (*end != '\0' || parsed == 0 || parsed > 600000) return false;
      out.timeout_ms = static_cast<unsigned>(parsed);
    } else return false;
  }
  return false;
}

std::string capture_version(const char* executable) {
  int pipefd[2]; if (::pipe2(pipefd, O_CLOEXEC) != 0) return {};
  const pid_t child = ::fork();
  if (child == 0) {
    ::dup2(pipefd[1], STDOUT_FILENO); ::dup2(pipefd[1], STDERR_FILENO);
    ::close(pipefd[0]); ::close(pipefd[1]);
    ::execl(executable, executable, "-version", nullptr); _exit(127);
  }
  ::close(pipefd[1]); std::string output; char buffer[1024];
  for (;;) {
    const auto count = ::read(pipefd[0], buffer, sizeof(buffer));
    if (count <= 0) break;
    if (output.size() < 65536) output.append(buffer, count);
  }
  ::close(pipefd[0]); int status{}; (void)::waitpid(child, &status, 0); return output;
}

void write_result(const Options& options, std::string_view outcome, int exit_code) {
  std::ofstream file(options.result, std::ios::trunc);
  file << "{\"schema\":1,\"outcome\":\"" << outcome
       << "\",\"exit_code\":" << exit_code << "}\n";
}
}

int main(int argc, char** argv) {
  Options options;
  if (!parse(argc, argv, options) || options.expected_version.empty() ||
      options.evidence.empty() || options.result.empty()) {
    std::fprintf(stderr, "usage: m9_app_runner --expected-version TEXT --evidence PATH --result PATH [--timeout-ms N] -- PROGRAM [ARGS...]\n");
    return 2;
  }
  if (capture_version(argv[options.command_index]).find(options.expected_version) ==
      std::string::npos) { write_result(options, "version_mismatch", -1); return 1; }

  int errors[2]; if (::pipe2(errors, O_CLOEXEC | O_NONBLOCK) != 0) return 2;
  const pid_t child = ::fork();
  if (child == 0) {
    ::dup2(errors[1], STDERR_FILENO); ::close(errors[0]); ::close(errors[1]);
    ::setenv("LC_ALL", "C", 1); ::setenv("LANG", "C", 1);
    ::setenv("XMODIFIERS", "@im=none", 1); ::setenv("SESSION_MANAGER", "", 1);
    ::setenv("XAUTHORITY", "/dev/null", 1);
    ::execvp(argv[options.command_index], argv + options.command_index); _exit(127);
  }
  ::close(errors[1]);
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(options.timeout_ms);
  std::string stderr_text; int status{}; bool evidence = false, exited = false;
  while (std::chrono::steady_clock::now() < deadline) {
    char buffer[1024]; const auto count = ::read(errors[0], buffer, sizeof(buffer));
    if (count > 0 && stderr_text.size() < 65536) stderr_text.append(buffer, count);
    if (::waitpid(child, &status, WNOHANG) == child) { exited = true; break; }
    if (std::filesystem::is_regular_file(options.evidence)) { evidence = true; break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (!exited) { ::kill(child, SIGTERM); (void)::waitpid(child, &status, 0); }
  ::close(errors[0]);
  std::string_view outcome = evidence ? "intentional_termination" :
      exited ? "application_crash" : "timeout";
  if (stderr_text.find("X Error") != std::string::npos) outcome = "x_protocol_error";
  if (stderr_text.find("RENDER") != std::string::npos ||
      stderr_text.find("SHAPE") != std::string::npos) outcome = "unexpected_extension_use";
  const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) :
                        WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1;
  write_result(options, outcome, exit_code);
  return outcome == "intentional_termination" ? 0 : 1;
}
