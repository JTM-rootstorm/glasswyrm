#include "output_client/output_client.hpp"
#include "tests/helpers/test_support.hpp"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <poll.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

using namespace glasswyrm::tools::output_client;
using gw::test::require;

namespace {

constexpr std::uint32_t kQueryFlags =
    GWIPC_OUTPUT_QUERY_DESCRIPTORS | GWIPC_OUTPUT_QUERY_MODES |
    GWIPC_OUTPUT_QUERY_LAYOUT | GWIPC_OUTPUT_QUERY_WINDOWS |
    GWIPC_OUTPUT_QUERY_VRR;

pid_t start_server(const char *program, const std::string &socket,
                   const char *mode) {
  std::filesystem::remove(socket);
  const auto child = ::fork();
  require(child >= 0, "fork fake output server");
  if (child == 0) {
    ::execl(program, program, socket.c_str(), mode, nullptr);
    _exit(127);
  }
  for (unsigned attempt = 0; attempt < 200; ++attempt) {
    if (std::filesystem::is_socket(socket))
      return child;
    int status = 0;
    if (::waitpid(child, &status, WNOHANG) == child)
      require(false, "fake output server exited before creating its socket");
    (void)::poll(nullptr, 0, 10);
  }
  require(false, "fake output server timed out creating its socket");
  return -1;
}

void stop_server(const pid_t child) {
  if (::kill(child, SIGTERM) < 0)
    require(errno == ESRCH, "terminate fake output server");
  int status = 0;
  require(::waitpid(child, &status, 0) == child, "reap fake output server");
}

void wait_server(const pid_t child) {
  int status = 0;
  require(::waitpid(child, &status, 0) == child && WIFEXITED(status) &&
              WEXITSTATUS(status) == 0,
          "fake output server exits successfully");
}

void test_typed_busy(const char *program, const std::string &socket) {
  const auto server = start_server(program, socket, "busy");
  Client client(socket);
  Snapshot snapshot;
  const auto result = client.query_once(kQueryFlags, snapshot);
  require(result.outcome == QueryOutcome::RetryableNotReady &&
              !result.detail.empty() && snapshot.generation == 0,
          "one query attempt exposes BUSY without a partial snapshot");
  stop_server(server);
}

void test_bounded_retry(const char *program, const std::string &socket) {
  const auto server = start_server(program, socket, "busy-ready");
  Client client(socket);
  Snapshot snapshot;
  std::string error;
  require(client.query(kQueryFlags, snapshot, error) &&
              snapshot.generation == 1 && snapshot.vrr_queried,
          "bounded retry completes on the retained connection: " + error);
  stop_server(server);
}

void test_reconnect(const char *program, const std::string &socket) {
  Client client(socket);
  auto server = start_server(program, socket, "close");
  Snapshot snapshot;
  const auto failed = client.query_once(kQueryFlags, snapshot);
  require(failed.outcome == QueryOutcome::Fatal && !failed.detail.empty(),
          "connection closure is a typed fatal transport result");
  wait_server(server);

  server = start_server(program, socket, "vrr-query");
  const auto reconnected = client.query_once(kQueryFlags, snapshot);
  require(reconnected.complete() && snapshot.generation == 1,
          "the next query destroys the closed handle and reconnects");
  stop_server(server);
}

} // namespace

int main(const int argc, char **argv) {
  require(argc == 2, "output-client outcome test receives fake server path");
  std::string directory = "/tmp/glasswyrm-output-outcomes-XXXXXX";
  require(::mkdtemp(directory.data()) != nullptr,
          "create output-client outcome directory");
  const auto socket = directory + "/control.sock";

  test_typed_busy(argv[1], socket);
  test_bounded_retry(argv[1], socket);
  test_reconnect(argv[1], socket);

  std::filesystem::remove_all(directory);
}
