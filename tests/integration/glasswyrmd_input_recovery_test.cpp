#include "helpers/synthetic_input_client.hpp"
#include "protocol/x11/event_mask.hpp"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
void require(const bool value, const char* message) {
  if (!value) {
    std::fprintf(stderr, "input recovery test: %s\n", message);
    std::exit(1);
  }
}

pid_t launch_peer(const char* executable, const std::string& socket,
                  const std::string& dump = {}) {
  const auto child = ::fork();
  require(child >= 0, "fork runtime peer");
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

pid_t launch_server(const char* executable, const std::string& directory,
                    const std::string& wm, const std::string& compositor,
                    const std::string& input) {
  const auto child = ::fork();
  require(child >= 0, "fork glasswyrmd");
  if (child == 0) {
    ::execl(executable, executable, "--display", "78", "--socket-dir",
            directory.c_str(), "--wm-socket", wm.c_str(),
            "--compositor-socket", compositor.c_str(),
            "--synthetic-input-socket", input.c_str(), nullptr);
    _exit(127);
  }
  return child;
}

void stop(const pid_t child) {
  if (child <= 0) return;
  (void)::kill(child, SIGTERM);
  int status = 0;
  (void)::waitpid(child, &status, 0);
}

bool wait_for(const std::filesystem::path& path) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    if (std::filesystem::exists(path)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

template <class Operation>
void require_disconnect(Operation operation, const char* message) {
  bool disconnected = false;
  try {
    operation();
  } catch (const std::runtime_error&) {
    disconnected = true;
  }
  require(disconnected, message);
}
}  // namespace

int main(int argc, char** argv) {
  require(argc == 4, "expected glasswyrmd, gwm, and gwcomp paths");
  char temporary[] = "/tmp/glasswyrmd-input-recovery-XXXXXX";
  require(::mkdtemp(temporary) != nullptr, "create temporary directory");
  const std::string root = temporary;
  const auto wm_socket = root + "/gwm.sock";
  const auto compositor_socket = root + "/gwcomp.sock";
  const auto input_socket = root + "/input.sock";

  const auto server = launch_server(argv[1], root, wm_socket,
                                    compositor_socket, input_socket);
  const auto wm = launch_peer(argv[2], wm_socket);
  const auto compositor =
      launch_peer(argv[3], compositor_socket, root + "/dump");
  require(wait_for(input_socket), "input listener appears after bootstrap");

  {
    gw::test::SyntheticInputClient provider(input_socket);
    const auto initial = provider.barrier(1);
    require(initial.time_ms == 1 && initial.state == 0,
            "initial barrier reports deterministic state");
    const auto motion = provider.motion(2, 4, 30, 40);
    require(motion.root_x == 30 && motion.root_y == 40 && motion.time_ms == 4,
            "motion advances retained pointer and time");
    const auto shift = provider.key(3, 5, 50, true);
    require((shift.state & gw::protocol::x11::state_mask::Shift) != 0 &&
                shift.time_ms == 5,
            "modifier press updates provider-owned state");
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  {
    gw::test::SyntheticInputClient provider(input_socket);
    const auto reset = provider.barrier(1);
    require(reset.time_ms == 5 && reset.root_x == 30 && reset.root_y == 40 &&
                reset.state == 0,
            "reconnect resets held state while retaining pointer and time");
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  {
    gw::test::SyntheticInputClient provider(input_socket);
    require_disconnect([&] { (void)provider.barrier(2); },
                       "skipped input ID disconnects only its provider");
  }
  {
    gw::test::SyntheticInputClient provider(input_socket);
    require_disconnect([&] { (void)provider.motion(1, 4, 0, 0); },
                       "backwards time disconnects only its provider");
  }
  {
    gw::test::SyntheticInputClient provider(input_socket);
    const auto survived = provider.barrier(1);
    require(survived.time_ms == 5 && survived.state == 0,
            "server accepts a fresh provider after malformed peers");
  }

  stop(server);
  stop(wm);
  stop(compositor);
  std::filesystem::remove_all(root);
  return 0;
}
