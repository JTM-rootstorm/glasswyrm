#include <glasswyrm/ipc.h>

#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace {

constexpr std::uint64_t kRequiredCapabilities =
    GWIPC_CAP_FD_PASSING | GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE |
    GWIPC_CAP_SURFACE_STATE | GWIPC_CAP_MEMFD_BUFFERS |
    GWIPC_CAP_DAMAGE_REGIONS | GWIPC_CAP_SDR_COLOR_METADATA |
    GWIPC_CAP_FRAME_ACKNOWLEDGEMENT;
constexpr std::uint64_t kProtocolCommonCapabilities =
    GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE | GWIPC_CAP_SURFACE_STATE |
    GWIPC_CAP_SDR_COLOR_METADATA | GWIPC_CAP_FRAME_ACKNOWLEDGEMENT;

[[noreturn]] void fail(const char* message) {
  std::fprintf(stderr, "gwcomp process test: %s\n", message);
  std::exit(1);
}

void require(bool condition, const char* message) {
  if (!condition) fail(message);
}

struct CommandResult {
  int status = -1;
  std::string output;
};

CommandResult command(const char* executable, const char* argument) {
  int output[2] = {-1, -1};
  require(::pipe(output) == 0, "create command output pipe");
  const pid_t child = ::fork();
  require(child >= 0, "fork command");
  if (child == 0) {
    (void)::dup2(output[1], STDOUT_FILENO);
    (void)::dup2(output[1], STDERR_FILENO);
    (void)::close(output[0]);
    (void)::close(output[1]);
    ::execl(executable, executable, argument, nullptr);
    _exit(127);
  }
  (void)::close(output[1]);
  CommandResult result;
  char buffer[512];
  for (;;) {
    const ssize_t count = ::read(output[0], buffer, sizeof(buffer));
    if (count > 0) result.output.append(buffer, static_cast<std::size_t>(count));
    if (count == 0) break;
    if (count < 0 && errno != EINTR) fail("read command output");
  }
  (void)::close(output[0]);
  require(::waitpid(child, &result.status, 0) == child, "wait for command");
  return result;
}

gwipc_status connect_as(const std::string& path, gwipc_role role,
                        std::uint64_t capabilities,
                        gwipc_connection** out_connection) {
  gwipc_connection_options options{};
  options.struct_size = sizeof(options);
  options.path = path.c_str();
  options.local_role = role;
  options.acceptable_server_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_COMPOSITOR);
  options.offered_capabilities = capabilities;
  options.required_peer_capabilities = 0;
  options.instance_label = "gwcomp-process-test";
  gwipc_status status = gwipc_connection_connect(&options, out_connection);
  if (status != GWIPC_STATUS_OK && status != GWIPC_STATUS_IN_PROGRESS)
    return status;
  for (int attempt = 0; attempt < 100; ++attempt) {
    pollfd descriptor{gwipc_connection_fd(*out_connection),
                      gwipc_connection_wanted_poll_events(*out_connection), 0};
    const int ready = ::poll(&descriptor, 1, 50);
    if (ready < 0 && errno != EINTR) return GWIPC_STATUS_SYSTEM_ERROR;
    if (ready > 0)
      status = gwipc_connection_process_poll_events(*out_connection,
                                                    descriptor.revents);
    const auto state = gwipc_connection_get_state(*out_connection);
    if (state == GWIPC_CONNECTION_ESTABLISHED) return GWIPC_STATUS_OK;
    if (state == GWIPC_CONNECTION_CLOSED) return status;
  }
  return GWIPC_STATUS_WOULD_BLOCK;
}

void require_rejected(const std::string& socket, gwipc_role role,
                      std::uint64_t capabilities, gwipc_status expected,
                      const char* message) {
  gwipc_connection* connection = nullptr;
  const auto status = connect_as(socket, role, capabilities, &connection);
  if (connection) gwipc_connection_destroy(connection);
  require(status == expected, message);
}

void require_valid_connection(const std::string& socket) {
  gwipc_connection* connection = nullptr;
  const auto status = connect_as(socket, GWIPC_ROLE_TEST_PRODUCER,
                                 kRequiredCapabilities, &connection);
  if (connection) gwipc_connection_destroy(connection);
  require(status == GWIPC_STATUS_OK,
          "valid producer connects after rejected isolated peer");
}

void require_role_specific_rejected(const std::string& socket) {
  gwipc_connection* connection = nullptr;
  (void)connect_as(socket, GWIPC_ROLE_PROTOCOL_SERVER,
                   kProtocolCommonCapabilities, &connection);
  require(connection != nullptr,
          "ProtocolServer common-capability connection is created");
  for (int attempt = 0; attempt < 100 &&
                        gwipc_connection_get_state(connection) !=
                            GWIPC_CONNECTION_CLOSED;
       ++attempt) {
    pollfd descriptor{gwipc_connection_fd(connection),
                      gwipc_connection_wanted_poll_events(connection), 0};
    const int ready = ::poll(&descriptor, 1, 20);
    if (ready > 0)
      (void)gwipc_connection_process_poll_events(connection,
                                                 descriptor.revents);
  }
  require(gwipc_connection_get_state(connection) == GWIPC_CONNECTION_CLOSED,
          "ProtocolServer missing WindowLifecycle is disconnected");
  gwipc_connection_destroy(connection);
}

void require_partial_output_model_rejected(const std::string& socket) {
  gwipc_connection* connection = nullptr;
  (void)connect_as(socket, GWIPC_ROLE_PROTOCOL_SERVER,
                   kProtocolCommonCapabilities | GWIPC_CAP_WINDOW_LIFECYCLE |
                       GWIPC_CAP_OUTPUT_MANAGEMENT,
                   &connection);
  require(connection != nullptr,
          "partial output-model connection is created");
  for (int attempt = 0; attempt < 100 &&
                        gwipc_connection_get_state(connection) !=
                            GWIPC_CONNECTION_CLOSED;
       ++attempt) {
    pollfd descriptor{gwipc_connection_fd(connection),
                      gwipc_connection_wanted_poll_events(connection), 0};
    const int ready = ::poll(&descriptor, 1, 20);
    if (ready > 0)
      (void)gwipc_connection_process_poll_events(connection,
                                                 descriptor.revents);
  }
  require(gwipc_connection_get_state(connection) == GWIPC_CONNECTION_CLOSED,
          "partial output-model capability bundle is disconnected");
  gwipc_connection_destroy(connection);
}

}  // namespace

int main(int argc, char** argv) {
  require(argc == 2, "usage: gwcomp_process_test /path/to/gwcomp");
  const char* executable = argv[1];

  const auto help = command(executable, "--help");
  require(WIFEXITED(help.status) && WEXITSTATUS(help.status) == 0,
          "--help exits successfully");
  require(help.output.find("Usage: gwcomp") != std::string::npos,
          "--help describes gwcomp usage");
  const auto version = command(executable, "--version");
  require(WIFEXITED(version.status) && WEXITSTATUS(version.status) == 0,
          "--version exits successfully");
  require(version.output.starts_with("gwcomp "),
          "--version identifies gwcomp");
  const auto missing = command(executable, "--once");
  require(WIFEXITED(missing.status) && WEXITSTATUS(missing.status) == 2,
          "missing required paths exits with command-line failure");
  require(missing.output.find("--ipc-socket and --dump-dir are required") !=
              std::string::npos,
          "missing required paths has a useful diagnostic");

  char temporary[] = "/tmp/gwcomp-process-test-XXXXXX";
  require(::mkdtemp(temporary) != nullptr, "create temporary directory");
  const std::string root = temporary;
  const std::string socket = root + "/gwcomp.sock";
  const std::string dumps = root + "/dumps";
  const pid_t child = ::fork();
  require(child >= 0, "fork gwcomp");
  if (child == 0) {
    ::execl(executable, executable, "--ipc-socket", socket.c_str(),
            "--dump-dir", dumps.c_str(), nullptr);
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
    require(::waitpid(child, &child_status, WNOHANG) == 0,
            "gwcomp remains alive while creating listener");
    (void)::usleep(10'000);
  }
  require(ready, "gwcomp listener becomes ready");
  require((status.st_mode & 0777) == 0600, "gwcomp socket has mode 0600");
  require(std::filesystem::is_directory(dumps),
          "gwcomp prepares its dump directory");

  require_rejected(socket, GWIPC_ROLE_DIAGNOSTIC_TOOL, kRequiredCapabilities,
                   GWIPC_STATUS_ROLE_REJECTED,
                   "wrong-role peer is rejected");
  require_valid_connection(socket);
  require_role_specific_rejected(socket);
  require_valid_connection(socket);
  require_partial_output_model_rejected(socket);
  require_valid_connection(socket);
  require_rejected(socket, GWIPC_ROLE_TEST_PRODUCER,
                   kRequiredCapabilities & ~GWIPC_CAP_FRAME_ACKNOWLEDGEMENT,
                   GWIPC_STATUS_CAPABILITY_MISMATCH,
                   "peer missing a required capability is rejected");
  require_valid_connection(socket);

  require(::kill(child, SIGTERM) == 0, "signal gwcomp");
  int child_status = 0;
  require(::waitpid(child, &child_status, 0) == child, "wait for gwcomp");
  require(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0,
          "SIGTERM stops gwcomp cleanly");
  require(::lstat(socket.c_str(), &status) != 0 && errno == ENOENT,
          "gwcomp unlinks its owned socket");
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return 0;
}
