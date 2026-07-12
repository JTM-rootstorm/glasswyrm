#include <glasswyrm/ipc.h>

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

[[noreturn]] void fail(const char* message) {
  std::fprintf(stderr, "gwm robustness test: %s\n", message);
  std::exit(1);
}
void require(bool condition, const char* message) {
  if (!condition) fail(message);
}

struct ConnectionDeleter {
  void operator()(gwipc_connection* value) const {
    gwipc_connection_destroy(value);
  }
};
struct MessageDeleter {
  void operator()(gwipc_message* value) const { gwipc_message_destroy(value); }
};
struct ContractPayloadDeleter {
  void operator()(gwipc_contract_payload* value) const {
    gwipc_contract_payload_destroy(value);
  }
};
struct ControlPayloadDeleter {
  void operator()(gwipc_control_payload* value) const {
    gwipc_control_payload_destroy(value);
  }
};
using Connection = std::unique_ptr<gwipc_connection, ConnectionDeleter>;
using Message = std::unique_ptr<gwipc_message, MessageDeleter>;

struct Process {
  pid_t pid{-1};
  std::string socket;
};

bool wait_for(pid_t child, int& status, int attempts = 500) {
  for (int attempt = 0; attempt < attempts; ++attempt) {
    const pid_t result = ::waitpid(child, &status, WNOHANG);
    if (result == child) return true;
    if (result < 0) return false;
    (void)::usleep(10'000);
  }
  return false;
}

Process start(const char* executable, const std::filesystem::path& root,
              const char* name, bool once = false) {
  Process process;
  process.socket = (root / (std::string(name) + ".sock")).string();
  process.pid = ::fork();
  require(process.pid >= 0, "fork gwm");
  if (process.pid == 0) {
    if (once)
      ::execl(executable, executable, "--ipc-socket", process.socket.c_str(),
              "--once", nullptr);
    else
      ::execl(executable, executable, "--ipc-socket", process.socket.c_str(),
              nullptr);
    _exit(127);
  }
  struct stat status {};
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (::lstat(process.socket.c_str(), &status) == 0 && S_ISSOCK(status.st_mode))
      return process;
    int child_status = 0;
    require(::waitpid(process.pid, &child_status, WNOHANG) == 0,
            "gwm remains alive during startup");
    (void)::usleep(10'000);
  }
  fail("gwm listener becomes ready");
}

bool drive(gwipc_connection* connection, int timeout = 20) {
  pollfd descriptor{gwipc_connection_fd(connection),
                    gwipc_connection_wanted_poll_events(connection), 0};
  const int ready = ::poll(&descriptor, 1, timeout);
  if (ready < 0) return errno == EINTR;
  if (ready != 0)
    (void)gwipc_connection_process_poll_events(connection, descriptor.revents);
  return gwipc_connection_get_state(connection) != GWIPC_CONNECTION_CLOSED;
}

Connection begin_connect(const std::string& socket, gwipc_role role,
                         std::uint64_t offered, std::uint64_t required) {
  gwipc_connection_options options{};
  options.struct_size = sizeof(options);
  options.path = socket.c_str();
  options.local_role = role;
  options.acceptable_server_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_WINDOW_MANAGER);
  options.offered_capabilities = offered;
  options.required_peer_capabilities = required;
  options.maximum_queued_bytes = GWIPC_HARD_MAXIMUM_QUEUED_BYTES;
  options.maximum_queued_messages = 8192;
  options.instance_label = "gwm-robustness-test";
  gwipc_connection* raw = nullptr;
  const auto status = gwipc_connection_connect(&options, &raw);
  require(status == GWIPC_STATUS_OK || status == GWIPC_STATUS_IN_PROGRESS,
          "begin GWIPC connection");
  return Connection(raw);
}

Connection connect_valid(const std::string& socket) {
  const auto caps = GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_WINDOW_POLICY;
  auto connection = begin_connect(socket, GWIPC_ROLE_PROTOCOL_SERVER, caps, caps);
  for (int attempt = 0; attempt < 300 &&
                        gwipc_connection_get_state(connection.get()) !=
                            GWIPC_CONNECTION_ESTABLISHED;
       ++attempt)
    require(drive(connection.get()), "valid connection remains live");
  require(gwipc_connection_get_state(connection.get()) ==
              GWIPC_CONNECTION_ESTABLISHED,
          "valid ProtocolServer establishes");
  return connection;
}

void expect_rejected(Connection connection) {
  for (int attempt = 0; attempt < 300 &&
                        gwipc_connection_get_state(connection.get()) !=
                            GWIPC_CONNECTION_CLOSED;
       ++attempt)
    (void)drive(connection.get());
  require(gwipc_connection_get_state(connection.get()) == GWIPC_CONNECTION_CLOSED,
          "incompatible peer is rejected");
}

template <class Value, class Encoder>
void send_contract(gwipc_connection* connection, std::uint16_t type,
                   std::uint32_t flags, const Value& value, Encoder encoder) {
  gwipc_contract_payload* raw = nullptr;
  require(encoder(&value, &raw) == GWIPC_STATUS_OK, "encode contract");
  const std::unique_ptr<gwipc_contract_payload, ContractPayloadDeleter> payload(raw);
  std::size_t size = 0;
  const auto* bytes = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message outgoing{};
  outgoing.struct_size = sizeof(outgoing);
  outgoing.type = type;
  outgoing.flags = flags;
  outgoing.payload = bytes;
  outgoing.payload_size = size;
  require(gwipc_connection_enqueue(connection, &outgoing) == GWIPC_STATUS_OK,
          "enqueue contract");
}

template <class Value, class Encoder>
void send_control(gwipc_connection* connection, std::uint16_t type,
                  const Value& value, Encoder encoder) {
  gwipc_control_payload* raw = nullptr;
  require(encoder(&value, &raw) == GWIPC_STATUS_OK, "encode control");
  const std::unique_ptr<gwipc_control_payload, ControlPayloadDeleter> payload(raw);
  std::size_t size = 0;
  const auto* bytes = gwipc_control_payload_data(payload.get(), &size);
  gwipc_outgoing_message outgoing{};
  outgoing.struct_size = sizeof(outgoing);
  outgoing.type = type;
  outgoing.payload = bytes;
  outgoing.payload_size = size;
  require(gwipc_connection_enqueue(connection, &outgoing) == GWIPC_STATUS_OK,
          "enqueue control");
}

void accepted_empty_policy(gwipc_connection* connection) {
  gwipc_snapshot_begin begin{};
  begin.struct_size = sizeof(begin);
  begin.snapshot_id = 1;
  begin.domain = GWIPC_SNAPSHOT_WINDOW_POLICY;
  begin.generation = 1;
  begin.expected_item_count = 1;
  send_control(connection, GWIPC_MESSAGE_SNAPSHOT_BEGIN, begin,
               gwipc_control_encode_snapshot_begin);
  gwipc_policy_context_upsert context{};
  context.struct_size = sizeof(context);
  context.root_window_id = context.workspace_id = 1;
  context.output_id = 1;
  context.work_width = 800;
  context.work_height = 600;
  send_contract(connection, GWIPC_MESSAGE_POLICY_CONTEXT_UPSERT,
                GWIPC_FLAG_SNAPSHOT_ITEM, context,
                gwipc_contract_encode_policy_context_upsert);
  gwipc_snapshot_end end{};
  end.struct_size = sizeof(end);
  end.snapshot_id = 1;
  end.generation = 1;
  end.actual_item_count = 1;
  send_control(connection, GWIPC_MESSAGE_SNAPSHOT_END, end,
               gwipc_control_encode_snapshot_end);
  gwipc_policy_commit commit{};
  commit.struct_size = sizeof(commit);
  commit.commit_id = 100;
  commit.producer_generation = 1;
  send_contract(connection, GWIPC_MESSAGE_POLICY_COMMIT,
                GWIPC_FLAG_ACK_REQUIRED, commit,
                gwipc_contract_encode_policy_commit);

  bool acknowledged = false;
  for (int attempt = 0; attempt < 300 && !acknowledged; ++attempt) {
    require(drive(connection), "drive accepted policy response");
    for (;;) {
      gwipc_message* raw = nullptr;
      if (gwipc_connection_receive(connection, &raw) != GWIPC_STATUS_OK) break;
      const Message message(raw);
      acknowledged = gwipc_message_type(message.get()) ==
                     GWIPC_MESSAGE_POLICY_ACKNOWLEDGED;
    }
  }
  require(acknowledged, "accepted empty policy is acknowledged");
}

void send_malformed(const std::string& socket) {
  const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
  require(fd >= 0, "create malformed peer socket");
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  require(socket.size() < sizeof(address.sun_path), "socket path fits sockaddr");
  std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", socket.c_str());
  require(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
          "connect malformed peer");
  const unsigned char garbage[]{0xde, 0xad, 0xbe, 0xef};
  require(::send(fd, garbage, sizeof(garbage), MSG_NOSIGNAL) ==
              static_cast<ssize_t>(sizeof(garbage)),
          "send malformed record");
  (void)::close(fd);
}

std::size_t descriptor_count(pid_t process) {
  const auto path = "/proc/" + std::to_string(process) + "/fd";
  DIR* directory = ::opendir(path.c_str());
  require(directory != nullptr, "open gwm descriptor directory");
  std::size_t count = 0;
  while (const auto* entry = ::readdir(directory))
    if (entry->d_name[0] != '.') ++count;
  (void)::closedir(directory);
  return count;
}

void assert_no_device_or_display_access(pid_t process) {
  const auto root = std::filesystem::path("/proc") / std::to_string(process) / "fd";
  for (const auto& entry : std::filesystem::directory_iterator(root)) {
    std::error_code error;
    const auto target = std::filesystem::read_symlink(entry.path(), error).string();
    if (error) continue;
    require(target.find("/tmp/.X11-unix/") == std::string::npos,
            "gwm opens no X11 display socket");
    require(target.find("/dev/dri/") == std::string::npos,
            "gwm opens no DRM device");
    require(target.find("/dev/input/") == std::string::npos,
            "gwm opens no input device");
    require(target.find("gwcomp.sock") == std::string::npos,
            "gwm opens no compositor socket");
  }
}

} // namespace

int main(int argc, char** argv) {
  require(argc == 2, "usage: gwm_robustness_test /path/to/gwm");
  char temporary[] = "/tmp/gwm-robustness-test-XXXXXX";
  require(::mkdtemp(temporary) != nullptr, "create temporary directory");
  const std::filesystem::path root = temporary;

  auto service = start(argv[1], root, "service");
  const auto capabilities = GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_WINDOW_POLICY;
  expect_rejected(begin_connect(service.socket, GWIPC_ROLE_TEST_PRODUCER,
                                capabilities, capabilities));
  expect_rejected(begin_connect(service.socket, GWIPC_ROLE_PROTOCOL_SERVER,
                                GWIPC_CAP_SNAPSHOTS, GWIPC_CAP_SNAPSHOTS));

  auto active = connect_valid(service.socket);
  auto waiting = begin_connect(service.socket, GWIPC_ROLE_PROTOCOL_SERVER,
                               capabilities, capabilities);
  for (int attempt = 0; attempt < 10; ++attempt) (void)drive(waiting.get());
  require(gwipc_connection_get_state(waiting.get()) !=
              GWIPC_CONNECTION_ESTABLISHED,
          "second peer cannot enter policy while one is active");
  active.reset();
  for (int attempt = 0; attempt < 300 &&
                        gwipc_connection_get_state(waiting.get()) !=
                            GWIPC_CONNECTION_ESTABLISHED;
       ++attempt)
    require(drive(waiting.get()), "waiting peer remains live");
  require(gwipc_connection_get_state(waiting.get()) ==
              GWIPC_CONNECTION_ESTABLISHED,
          "waiting peer establishes after active disconnect");
  waiting.reset();

  send_malformed(service.socket);
  (void)::usleep(50'000);
  auto after_malformed = connect_valid(service.socket);
  after_malformed.reset();

  const auto baseline_descriptors = descriptor_count(service.pid);
  for (int iteration = 0; iteration < 64; ++iteration) {
    auto connection = connect_valid(service.socket);
    connection.reset();
  }
  (void)::usleep(50'000);
  require(descriptor_count(service.pid) <= baseline_descriptors + 1,
          "repeated reconnects do not leak descriptors");
  assert_no_device_or_display_access(service.pid);
  require(::kill(service.pid, SIGTERM) == 0, "signal running gwm");
  int status = 0;
  require(wait_for(service.pid, status), "SIGTERM gwm exits promptly");
  require(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "SIGTERM is a clean exit");
  require(!std::filesystem::exists(service.socket),
          "SIGTERM unlinks listener socket");

  auto once = start(argv[1], root, "once", true);
  auto producer = connect_valid(once.socket);
  accepted_empty_policy(producer.get());
  producer.reset();
  require(wait_for(once.pid, status), "--once exits after accepted peer disconnect");
  require(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "--once exits successfully");
  require(!std::filesystem::exists(once.socket), "--once unlinks listener socket");

  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return 0;
}
