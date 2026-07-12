#include <glasswyrm/ipc.h>

#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

namespace {

[[noreturn]] void fail(const char* message) {
  std::fprintf(stderr, "gwm process test: %s\n", message);
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
struct ContractDeleter {
  void operator()(gwipc_decoded_contract* value) const {
    gwipc_decoded_contract_destroy(value);
  }
};
struct ControlDeleter {
  void operator()(gwipc_decoded_control* value) const {
    gwipc_decoded_control_destroy(value);
  }
};
using Connection = std::unique_ptr<gwipc_connection, ConnectionDeleter>;
using Message = std::unique_ptr<gwipc_message, MessageDeleter>;

bool pump(gwipc_connection* connection, int timeout = 50) {
  pollfd descriptor{gwipc_connection_fd(connection),
                    gwipc_connection_wanted_poll_events(connection), 0};
  const int ready = ::poll(&descriptor, 1, timeout);
  if (ready < 0) return errno == EINTR;
  if (ready != 0 && gwipc_connection_process_poll_events(
                        connection, descriptor.revents) ==
                        GWIPC_STATUS_SYSTEM_ERROR)
    return false;
  return gwipc_connection_get_state(connection) != GWIPC_CONNECTION_CLOSED;
}

Connection connect_to(const std::string& socket) {
  gwipc_connection_options options{};
  options.struct_size = sizeof(options);
  options.path = socket.c_str();
  options.local_role = GWIPC_ROLE_PROTOCOL_SERVER;
  options.acceptable_server_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_WINDOW_MANAGER);
  options.offered_capabilities = GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_WINDOW_POLICY;
  options.required_peer_capabilities = options.offered_capabilities;
  options.maximum_queued_bytes = GWIPC_HARD_MAXIMUM_QUEUED_BYTES;
  options.maximum_queued_messages = 8192;
  options.instance_label = "gwm-process-test";
  gwipc_connection* raw = nullptr;
  const auto status = gwipc_connection_connect(&options, &raw);
  require(status == GWIPC_STATUS_OK || status == GWIPC_STATUS_IN_PROGRESS,
          "connect producer");
  Connection connection(raw);
  for (int attempt = 0; attempt < 200 &&
                        gwipc_connection_get_state(connection.get()) !=
                            GWIPC_CONNECTION_ESTABLISHED;
       ++attempt)
    require(pump(connection.get()), "drive producer handshake");
  require(gwipc_connection_get_state(connection.get()) ==
              GWIPC_CONNECTION_ESTABLISHED,
          "producer handshake establishes");
  return connection;
}

template <class Value, class Encoder>
void send_contract(gwipc_connection* connection, std::uint16_t type,
                   std::uint32_t flags, const Value& value, Encoder encoder) {
  gwipc_contract_payload* raw = nullptr;
  require(encoder(&value, &raw) == GWIPC_STATUS_OK, "encode policy contract");
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
          "enqueue policy contract");
}

template <class Value, class Encoder>
void send_control(gwipc_connection* connection, std::uint16_t type,
                  const Value& value, Encoder encoder) {
  gwipc_control_payload* raw = nullptr;
  require(encoder(&value, &raw) == GWIPC_STATUS_OK, "encode snapshot control");
  const std::unique_ptr<gwipc_control_payload, ControlPayloadDeleter> payload(raw);
  std::size_t size = 0;
  const auto* bytes = gwipc_control_payload_data(payload.get(), &size);
  gwipc_outgoing_message outgoing{};
  outgoing.struct_size = sizeof(outgoing);
  outgoing.type = type;
  outgoing.payload = bytes;
  outgoing.payload_size = size;
  require(gwipc_connection_enqueue(connection, &outgoing) == GWIPC_STATUS_OK,
          "enqueue snapshot control");
}

void send_policy(gwipc_connection* connection) {
  gwipc_snapshot_begin begin{};
  begin.struct_size = sizeof(begin);
  begin.snapshot_id = 1;
  begin.domain = GWIPC_SNAPSHOT_WINDOW_POLICY;
  begin.generation = 1;
  begin.expected_item_count = 2;
  send_control(connection, GWIPC_MESSAGE_SNAPSHOT_BEGIN, begin,
               gwipc_control_encode_snapshot_begin);

  gwipc_policy_context_upsert context{};
  context.struct_size = sizeof(context);
  context.root_window_id = 1;
  context.workspace_id = 1;
  context.output_id = 1;
  context.work_width = 1024;
  context.work_height = 768;
  send_contract(connection, GWIPC_MESSAGE_POLICY_CONTEXT_UPSERT,
                GWIPC_FLAG_SNAPSHOT_ITEM, context,
                gwipc_contract_encode_policy_context_upsert);

  gwipc_policy_window_upsert window{};
  window.struct_size = sizeof(window);
  window.window_id = 1001;
  window.parent_window_id = 1;
  window.workspace_id = 1;
  window.requested_width = 320;
  window.requested_height = 240;
  window.window_type = GWIPC_POLICY_WINDOW_NORMAL;
  window.map_intent = GWIPC_POLICY_WANTS_MAP;
  window.decoration_preference = GWIPC_TRI_STATE_UNKNOWN;
  window.creation_serial = 1;
  window.map_serial = 1;
  send_contract(connection, GWIPC_MESSAGE_POLICY_WINDOW_UPSERT,
                GWIPC_FLAG_SNAPSHOT_ITEM, window,
                gwipc_contract_encode_policy_window_upsert);

  gwipc_snapshot_end end{};
  end.struct_size = sizeof(end);
  end.snapshot_id = 1;
  end.generation = 1;
  end.actual_item_count = 2;
  send_control(connection, GWIPC_MESSAGE_SNAPSHOT_END, end,
               gwipc_control_encode_snapshot_end);

  gwipc_policy_commit commit{};
  commit.struct_size = sizeof(commit);
  commit.commit_id = 100;
  commit.producer_generation = 1;
  send_contract(connection, GWIPC_MESSAGE_POLICY_COMMIT,
                GWIPC_FLAG_ACK_REQUIRED, commit,
                gwipc_contract_encode_policy_commit);
}

void receive_policy(gwipc_connection* connection) {
  constexpr std::uint16_t expected_types[] = {
      GWIPC_MESSAGE_SNAPSHOT_BEGIN, GWIPC_MESSAGE_POLICY_WINDOW_STATE,
      GWIPC_MESSAGE_SNAPSHOT_END, GWIPC_MESSAGE_POLICY_ACKNOWLEDGED};
  std::size_t received = 0;
  for (int attempt = 0; attempt < 300 && received < std::size(expected_types);
       ++attempt) {
    require(pump(connection), "drive policy response");
    for (;;) {
      gwipc_message* raw = nullptr;
      if (gwipc_connection_receive(connection, &raw) != GWIPC_STATUS_OK) break;
      const Message message(raw);
      require(gwipc_message_type(message.get()) == expected_types[received],
              "policy response ordering");
      if (received == 0 || received == 2) {
        gwipc_decoded_control* decoded_raw = nullptr;
        require(gwipc_control_decode_message(message.get(), &decoded_raw) ==
                    GWIPC_STATUS_OK,
                "decode output snapshot control");
        const std::unique_ptr<gwipc_decoded_control, ControlDeleter> decoded(decoded_raw);
        if (received == 0) {
          const auto* begin = gwipc_decoded_snapshot_begin(decoded.get());
          require(begin && begin->domain == GWIPC_SNAPSHOT_WINDOW_POLICY &&
                      begin->expected_item_count == 1,
                  "output snapshot identity and count");
        }
      } else {
        gwipc_decoded_contract* decoded_raw = nullptr;
        require(gwipc_contract_decode_message(message.get(), &decoded_raw) ==
                    GWIPC_STATUS_OK,
                "decode output policy contract");
        const std::unique_ptr<gwipc_decoded_contract, ContractDeleter> decoded(decoded_raw);
        if (received == 1) {
          const auto* state = gwipc_decoded_policy_window_state(decoded.get());
          require(state && state->window_id == 1001 && state->visible &&
                      state->focused && state->stacking == 0,
                  "deterministic policy window state");
        } else {
          const auto* ack = gwipc_decoded_policy_acknowledged(decoded.get());
          require(ack && ack->commit_id == 100 &&
                      ack->result == GWIPC_POLICY_ACCEPTED &&
                      ack->applied_generation == 1 && ack->window_count == 1 &&
                      ack->policy_hash != 0,
                  "correlated accepted acknowledgement");
        }
      }
      ++received;
    }
  }
  require(received == std::size(expected_types), "complete policy response arrives");
}

} // namespace

int main(int argc, char** argv) {
  require(argc == 2, "usage: gwm_process_test /path/to/gwm");
  char temporary[] = "/tmp/gwm-process-test-XXXXXX";
  require(::mkdtemp(temporary) != nullptr, "create temporary directory");
  const std::filesystem::path root = temporary;
  const auto socket = (root / "gwm.sock").string();
  const pid_t child = ::fork();
  require(child >= 0, "fork gwm");
  if (child == 0) {
    ::execl(argv[1], argv[1], "--ipc-socket", socket.c_str(), "--max-commits",
            "1", nullptr);
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
            "gwm remains alive during startup");
    (void)::usleep(10'000);
  }
  require(ready, "gwm listener becomes ready");
  require((status.st_mode & 0777U) == 0600U, "listener permissions are private");
  auto connection = connect_to(socket);
  send_policy(connection.get());
  receive_policy(connection.get());
  connection.reset();
  int child_status = 0;
  require(::waitpid(child, &child_status, 0) == child, "wait for gwm");
  require(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0,
          "gwm exits after maximum accepted commits");
  require(!std::filesystem::exists(socket), "listener socket is unlinked");
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return 0;
}
