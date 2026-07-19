#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
void require(bool value, const char *message) {
  if (!value) {
    std::fprintf(stderr, "integrated startup test: %s\n", message);
    std::exit(1);
  }
}
pid_t launch_server(const char *executable, const std::string &dir,
                    const std::string &wm, const std::string &comp,
                    const char *display) {
  const auto child = ::fork();
  require(child >= 0, "fork glasswyrmd");
  if (child == 0) {
    ::execl(executable, executable, "--display", display, "--socket-dir",
            dir.c_str(), "--wm-socket", wm.c_str(), "--compositor-socket",
            comp.c_str(), nullptr);
    _exit(127);
  }
  return child;
}
pid_t launch_output_model_server(const char *executable,
                                 const std::string &dir,
                                 const std::string &wm,
                                 const std::string &comp,
                                 const char *display) {
  const auto child = ::fork();
  require(child >= 0, "fork output-model glasswyrmd");
  if (child == 0) {
    ::execl(executable, executable, "--display", display, "--socket-dir",
            dir.c_str(), "--wm-socket", wm.c_str(), "--compositor-socket",
            comp.c_str(), "--output-model", nullptr);
    _exit(127);
  }
  return child;
}
pid_t launch_peer(const char *executable, const std::string &socket,
                  const std::string &dump = {}) {
  const auto child = ::fork();
  require(child >= 0, "fork peer");
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
pid_t launch_output_model_compositor(const char *executable,
                                     const std::string &socket,
                                     const std::string &dump) {
  const auto child = ::fork();
  require(child >= 0, "fork output-model compositor");
  if (child == 0) {
    ::execl(executable, executable, "--ipc-socket", socket.c_str(),
            "--dump-dir", dump.c_str(), "--headless-output",
            "LEFT:800x600@60000", "--headless-output",
            "RIGHT:640x480@75000", nullptr);
    _exit(127);
  }
  return child;
}
void stop(pid_t child) {
  if (child <= 0)
    return;
  (void)::kill(child, SIGTERM);
  int status = 0;
  (void)::waitpid(child, &status, 0);
}
bool wait_for(const std::filesystem::path &path, bool exists,
              std::chrono::milliseconds duration) {
  const auto deadline = std::chrono::steady_clock::now() + duration;
  while (std::chrono::steady_clock::now() < deadline) {
    if (std::filesystem::exists(path) == exists)
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}
} // namespace

int main(int argc, char **argv) {
  require(argc == 4, "expected glasswyrmd, gwm, and gwcomp paths");
  char temporary[] = "/tmp/glasswyrmd-integrated-XXXXXX";
  require(::mkdtemp(temporary) != nullptr, "create temporary directory");
  const std::string root = temporary;
  const auto wm_socket = root + "/gwm.sock";
  const auto comp_socket = root + "/gwcomp.sock";
  const auto x_socket = std::filesystem::path(root) / "X71";
  const auto server =
      launch_server(argv[1], root, wm_socket, comp_socket, "71");
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  require(!std::filesystem::exists(x_socket),
          "X listener remains absent before peer bootstrap");
  const auto wm = launch_peer(argv[2], wm_socket);
  const auto comp = launch_peer(argv[3], comp_socket, root + "/dump");
  require(wait_for(x_socket, true, std::chrono::seconds(5)),
          "X listener appears after both peer bootstraps");
  stop(server);
  stop(wm);
  stop(comp);

  const auto output_root = root + "/output-model";
  std::filesystem::create_directory(output_root);
  const auto output_wm_socket = output_root + "/gwm.sock";
  const auto output_comp_socket = output_root + "/gwcomp.sock";
  const auto output_x_socket = std::filesystem::path(output_root) / "X73";
  const auto output_server = launch_output_model_server(
      argv[1], output_root, output_wm_socket, output_comp_socket, "73");
  const auto output_comp = launch_output_model_compositor(
      argv[3], output_comp_socket, output_root + "/dump");
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  require(!std::filesystem::exists(output_x_socket),
          "output-model listener waits for accepted zero-window policy");
  const auto output_wm = launch_peer(argv[2], output_wm_socket);
  require(wait_for(output_x_socket, true, std::chrono::seconds(5)),
          "output-model listener follows inventory and policy bootstrap");
  stop(output_server);
  stop(output_wm);
  stop(output_comp);

  const auto timeout_root = root + "/timeout";
  std::filesystem::create_directory(timeout_root);
  const auto timed =
      launch_server(argv[1], timeout_root, timeout_root + "/missing-wm",
                    timeout_root + "/missing-comp", "72");
  int status = 0;
  require(::waitpid(timed, &status, 0) == timed, "wait for bootstrap timeout");
  require(WIFEXITED(status) && WEXITSTATUS(status) != 0,
          "bootstrap timeout exits nonzero");
  require(!std::filesystem::exists(std::filesystem::path(timeout_root) / "X72"),
          "timeout never creates X listener");
  return 0;
}
