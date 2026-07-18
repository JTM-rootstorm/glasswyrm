#include "helpers/synthetic_input_client.hpp"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

void require(const bool value, const char* message) {
  if (!value) {
    std::fprintf(stderr, "synthetic cursor test: %s\n", message);
    std::exit(1);
  }
}

pid_t launch(const char* executable,
             const std::initializer_list<std::string>& arguments) {
  const auto child = ::fork();
  require(child >= 0, "fork process");
  if (child == 0) {
    std::vector<const char*> argv{executable};
    for (const auto& argument : arguments) argv.push_back(argument.c_str());
    argv.push_back(nullptr);
    ::execv(executable, const_cast<char* const*>(argv.data()));
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

bool wait_for_file(const std::filesystem::path& path) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    if (std::filesystem::exists(path)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

bool wait_for_cursor_membership(const std::filesystem::path& manifest,
                                const std::int32_t x, const std::int32_t y,
                                const bool expect_empty,
                                std::string* membership = nullptr) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(5);
  const auto position = "\"x\":" + std::to_string(x) +
                        ",\"y\":" + std::to_string(y);
  while (std::chrono::steady_clock::now() < deadline) {
    std::ifstream input(manifest);
    const std::string contents{std::istreambuf_iterator<char>(input), {}};
    std::string latest_membership;
    bool found = false;
    for (auto cursor = contents.find(position); cursor != std::string::npos;
         cursor = contents.find(position, cursor + position.size())) {
      const auto start = contents.find("\"memberships\":[", cursor);
      const auto end = start == std::string::npos
                           ? std::string::npos
                           : contents.find(']', start);
      if (start == std::string::npos || end == std::string::npos) continue;
      const auto values = start + sizeof("\"memberships\":[") - 1;
      latest_membership = contents.substr(values, end - values);
      found = true;
    }
    if (found && latest_membership.empty() == expect_empty) {
      if (membership) *membership = latest_membership;
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  require(argc == 4, "expected glasswyrmd, gwm, and gwcomp paths");
  char temporary[] = "/tmp/glasswyrmd-synthetic-cursor-XXXXXX";
  require(::mkdtemp(temporary) != nullptr, "create temporary directory");
  const std::filesystem::path root = temporary;
  const auto wm_socket = (root / "gwm.sock").string();
  const auto compositor_socket = (root / "gwcomp.sock").string();
  const auto input_socket = (root / "input.sock").string();
  const auto manifest = root / "scene.jsonl";
  const auto dumps = (root / "dumps").string();

  const auto wm = launch(argv[2], {"--ipc-socket", wm_socket});
  const auto compositor = launch(
      argv[3], {"--backend", "headless", "--ipc-socket", compositor_socket,
                "--dump-dir", dumps, "--scene-manifest", manifest.string(),
                "--headless-output", "LEFT:640x480@60000",
                "--headless-output", "RIGHT:800x600@60000"});
  const auto server = launch(
      argv[1], {"--display", "79", "--socket-dir", root.string(),
                "--wm-socket", wm_socket, "--compositor-socket",
                compositor_socket, "--software-content", "--output-model",
                "--synthetic-input-socket", input_socket});

  require(wait_for_file(input_socket), "synthetic input listener appears");
  std::string left_membership;
  std::string right_membership;
  {
    gw::test::SyntheticInputClient provider(input_socket);
    const auto left = provider.motion(1, 2, 100, 100);
    require(left.root_x == 100 && left.root_y == 100,
            "synthetic motion reaches the left output");
    require(wait_for_cursor_membership(manifest, 100, 100, false,
                                       &left_membership),
            "scene v2 publishes the connected cursor on the left output");
    const auto right = provider.motion(2, 3, 900, 100);
    require(right.root_x == 900 && right.root_y == 100,
            "synthetic motion reaches the right output");
    require(wait_for_cursor_membership(manifest, 900, 100, false,
                                       &right_membership),
            "scene v2 publishes the connected cursor on the right output");
    require(left_membership != right_membership,
            "cursor output membership changes across the output boundary");
  }
  require(wait_for_cursor_membership(manifest, 900, 100, true),
          "synthetic provider disconnect hides the cursor membership");

  stop(server);
  stop(wm);
  stop(compositor);
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return 0;
}
