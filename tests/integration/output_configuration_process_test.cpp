#include <glasswyrm/ipc.h>

#include "tests/helpers/test_support.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <memory>
#include <poll.h>
#include <string>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

using gw::test::require;

struct Child {
  pid_t pid{-1};
  Child() = default;
  explicit Child(const pid_t value) : pid(value) {}
  Child(const Child&) = delete;
  Child& operator=(const Child&) = delete;
  Child(Child&& other) noexcept : pid(std::exchange(other.pid, -1)) {}
  ~Child() { stop(); }
  void stop() noexcept {
    if (pid > 0) {
      (void)::kill(pid, SIGTERM);
      (void)::waitpid(pid, nullptr, 0);
      pid = -1;
    }
  }
  bool wait_success() noexcept {
    if (pid <= 0)
      return false;
    int status = 0;
    const auto result = ::waitpid(pid, &status, 0);
    pid = -1;
    return result > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
  }
};

Child launch(const char* executable,
             const std::vector<std::string>& arguments) {
  const auto child = ::fork();
  require(child >= 0, "fork output configuration process");
  if (child == 0) {
    (void)::prctl(PR_SET_PDEATHSIG, SIGTERM);
    std::vector<char*> values{const_cast<char*>(executable)};
    for (const auto& argument : arguments)
      values.push_back(const_cast<char*>(argument.c_str()));
    values.push_back(nullptr);
    ::execv(executable, values.data());
    _exit(127);
  }
  return Child(child);
}

void wait_for_socket(const std::string& path) {
  for (unsigned attempt = 0; attempt < 500; ++attempt) {
    struct stat status{};
    if (::lstat(path.c_str(), &status) == 0 && S_ISSOCK(status.st_mode))
      return;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  require(false, "socket readiness timeout: " + path);
}

struct ConnectionDelete {
  void operator()(gwipc_connection* value) const noexcept {
    gwipc_connection_destroy(value);
  }
};

using Connection = std::unique_ptr<gwipc_connection, ConnectionDelete>;

bool pump(gwipc_connection* connection) {
  pollfd descriptor{gwipc_connection_fd(connection),
                    gwipc_connection_wanted_poll_events(connection), 0};
  const auto count = ::poll(&descriptor, 1, 50);
  if (count < 0)
    return false;
  if (count != 0)
    (void)gwipc_connection_process_poll_events(connection,
                                               descriptor.revents);
  return gwipc_connection_get_state(connection) != GWIPC_CONNECTION_CLOSED;
}

Connection connect_tool(const std::string& path) {
  gwipc_connection_options options{};
  options.struct_size = sizeof(options);
  options.path = path.c_str();
  options.local_role = GWIPC_ROLE_DIAGNOSTIC_TOOL;
  options.acceptable_server_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_PROTOCOL_SERVER);
  options.offered_capabilities =
      GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE | GWIPC_CAP_OUTPUT_CONTROL;
  options.required_peer_capabilities = GWIPC_CAP_OUTPUT_CONTROL;
  options.maximum_payload = 4096;
  options.maximum_fd_count = 0;
  options.maximum_queued_bytes = 2U * 1024U * 1024U;
  options.maximum_queued_messages = 2048;
  options.instance_label = "output-configuration-process-test";
  gwipc_connection* raw = nullptr;
  const auto status = gwipc_connection_connect(&options, &raw);
  require(status == GWIPC_STATUS_OK || status == GWIPC_STATUS_IN_PROGRESS,
          "connect output configuration tool");
  Connection connection(raw);
  for (unsigned attempt = 0; attempt < 300 &&
                             gwipc_connection_get_state(connection.get()) !=
                                 GWIPC_CONNECTION_ESTABLISHED;
       ++attempt)
    require(pump(connection.get()), "drive output tool handshake");
  require(gwipc_connection_get_state(connection.get()) ==
              GWIPC_CONNECTION_ESTABLISHED,
          "output tool establishes");
  return connection;
}

template <typename Value>
void send_contract(gwipc_connection* connection, const std::uint16_t type,
                   const std::uint32_t flags, const Value& value,
                   gwipc_status (*encode)(const Value*,
                                          gwipc_contract_payload**)) {
  gwipc_contract_payload* raw = nullptr;
  require(encode(&value, &raw) == GWIPC_STATUS_OK,
          "encode output configuration contract");
  std::unique_ptr<gwipc_contract_payload,
                  decltype(&gwipc_contract_payload_destroy)>
      payload(raw, gwipc_contract_payload_destroy);
  std::size_t size = 0;
  const auto* data = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.flags = flags;
  message.payload = data;
  message.payload_size = size;
  require(gwipc_connection_enqueue(connection, &message) == GWIPC_STATUS_OK,
          "enqueue output configuration contract");
}

template <typename Value>
void send_control(gwipc_connection* connection, const std::uint16_t type,
                  const Value& value,
                  gwipc_status (*encode)(const Value*,
                                         gwipc_control_payload**)) {
  gwipc_control_payload* raw = nullptr;
  require(encode(&value, &raw) == GWIPC_STATUS_OK,
          "encode output configuration snapshot control");
  std::unique_ptr<gwipc_control_payload,
                  decltype(&gwipc_control_payload_destroy)>
      payload(raw, gwipc_control_payload_destroy);
  std::size_t size = 0;
  const auto* data = gwipc_control_payload_data(payload.get(), &size);
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.payload = data;
  message.payload_size = size;
  require(gwipc_connection_enqueue(connection, &message) == GWIPC_STATUS_OK,
          "enqueue output configuration snapshot control");
}

struct Reply {
  std::uint64_t generation{};
  std::uint64_t primary_output{};
  std::uint32_t root_width{};
  std::uint32_t root_height{};
  gwipc_output_configuration_result result{
      GWIPC_OUTPUT_CONFIGURATION_INTERNAL_ERROR};
  std::vector<gwipc_output_upsert> outputs;
};

Reply receive_reply(gwipc_connection* connection) {
  Reply reply;
  for (unsigned attempt = 0; attempt < 800; ++attempt) {
    require(pump(connection), "drive output configuration reply");
    while (true) {
      gwipc_message* raw = nullptr;
      const auto status = gwipc_connection_receive(connection, &raw);
      if (status == GWIPC_STATUS_WOULD_BLOCK)
        break;
      require(status == GWIPC_STATUS_OK,
              "receive output configuration record");
      std::unique_ptr<gwipc_message, decltype(&gwipc_message_destroy)> message(
          raw, gwipc_message_destroy);
      if (gwipc_message_type(message.get()) == GWIPC_MESSAGE_OUTPUT_UPSERT) {
        gwipc_decoded_contract* decoded_raw = nullptr;
        require(gwipc_contract_decode_message(message.get(), &decoded_raw) ==
                    GWIPC_STATUS_OK,
                "decode queried output state");
        std::unique_ptr<gwipc_decoded_contract,
                        decltype(&gwipc_decoded_contract_destroy)>
            decoded(decoded_raw, gwipc_decoded_contract_destroy);
        const auto* output = gwipc_decoded_output_upsert(decoded.get());
        require(output != nullptr, "queried output state is typed");
        reply.outputs.push_back(*output);
      }
      if (gwipc_message_type(message.get()) !=
          GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED)
        continue;
      gwipc_decoded_contract* decoded_raw = nullptr;
      require(gwipc_contract_decode_message(message.get(), &decoded_raw) ==
                  GWIPC_STATUS_OK,
              "decode output configuration acknowledgement");
      std::unique_ptr<gwipc_decoded_contract,
                      decltype(&gwipc_decoded_contract_destroy)>
          decoded(decoded_raw, gwipc_decoded_contract_destroy);
      const auto* acknowledged =
          gwipc_decoded_output_configuration_acknowledged(decoded.get());
      require(acknowledged != nullptr,
              "output configuration acknowledgement is typed");
      reply.generation = acknowledged->applied_generation;
      reply.primary_output = acknowledged->primary_output_id;
      reply.root_width = acknowledged->root_logical_width;
      reply.root_height = acknowledged->root_logical_height;
      reply.result = acknowledged->result;
      return reply;
    }
  }
  require(false, "output configuration reply timeout");
  return {};
}

Reply query(gwipc_connection* connection, const std::uint64_t id) {
  gwipc_output_state_query query{};
  query.struct_size = sizeof(query);
  query.query_id = id;
  query.flags = GWIPC_OUTPUT_QUERY_LAYOUT;
  send_contract(connection, GWIPC_MESSAGE_OUTPUT_STATE_QUERY,
                GWIPC_FLAG_ACK_REQUIRED, query,
                gwipc_contract_encode_output_state_query);
  return receive_reply(connection);
}

Reply configure(gwipc_connection* connection, const std::uint64_t id,
                const Reply& initial) {
  require(initial.outputs.size() == 2, "fixture exposes two outputs");
  auto outputs = initial.outputs;
  outputs[0].logical_x = 0;
  outputs[0].logical_y = 0;
  outputs[1].logical_x = 0;
  outputs[1].logical_y = 480;

  gwipc_snapshot_begin begin{};
  begin.struct_size = sizeof(begin);
  begin.snapshot_id = id;
  begin.domain = GWIPC_SNAPSHOT_OUTPUTS;
  begin.generation = initial.generation;
  begin.expected_item_count = outputs.size();
  send_control(connection, GWIPC_MESSAGE_SNAPSHOT_BEGIN, begin,
               gwipc_control_encode_snapshot_begin);
  for (const auto& output : outputs)
    send_contract(connection, GWIPC_MESSAGE_OUTPUT_UPSERT,
                  GWIPC_FLAG_SNAPSHOT_ITEM, output,
                  gwipc_contract_encode_output_upsert);
  gwipc_snapshot_end end{};
  end.struct_size = sizeof(end);
  end.snapshot_id = id;
  end.generation = initial.generation;
  end.actual_item_count = outputs.size();
  send_control(connection, GWIPC_MESSAGE_SNAPSHOT_END, end,
               gwipc_control_encode_snapshot_end);
  gwipc_output_configuration_commit commit{};
  commit.struct_size = sizeof(commit);
  commit.configuration_id = id;
  commit.base_generation = initial.generation;
  commit.primary_output_id = outputs[1].output_id;
  send_contract(connection, GWIPC_MESSAGE_OUTPUT_CONFIGURATION_COMMIT,
                GWIPC_FLAG_ACK_REQUIRED, commit,
                gwipc_contract_encode_output_configuration_commit);
  return receive_reply(connection);
}

} // namespace

int main(int argc, char** argv) {
  require(argc == 5,
          "expected glasswyrmd, gwm, gwcomp, and X11 probe paths");
  std::string directory = "/tmp/glasswyrm-output-transaction-XXXXXX";
  require(::mkdtemp(directory.data()) != nullptr,
          "create output transaction directory");
  const auto wm = directory + "/gwm.sock";
  const auto compositor = directory + "/gwcomp.sock";
  const auto control = directory + "/control.sock";
  const auto x11_dir = directory + "/x11";
  const auto dumps = directory + "/dumps";
  std::filesystem::create_directories(x11_dir);
  std::filesystem::create_directories(dumps);

  auto wm_process = launch(argv[2], {"--ipc-socket", wm});
  auto compositor_process =
      launch(argv[3], {"--backend", "headless", "--ipc-socket", compositor,
                       "--dump-dir", dumps, "--headless-output",
                       "LEFT:640x480@60000", "--headless-output",
                       "RIGHT:640x480@60000"});
  wait_for_socket(wm);
  wait_for_socket(compositor);
  auto server = launch(argv[1], {"--display", "91", "--socket-dir", x11_dir,
                                 "--wm-socket", wm, "--compositor-socket",
                                 compositor, "--output-model",
                                 "--control-socket", control});
  wait_for_socket(control);

  auto tool = connect_tool(control);
  const auto initial = query(tool.get(), 101);
  require(initial.result == GWIPC_OUTPUT_CONFIGURATION_ACCEPTED &&
              initial.generation == 1 && initial.root_width == 1280 &&
              initial.root_height == 480,
          "initial query reports the horizontal two-output layout");
  const auto accepted = configure(tool.get(), 102, initial);
  require(accepted.result == GWIPC_OUTPUT_CONFIGURATION_ACCEPTED &&
              accepted.generation == 2 && accepted.root_width == 640 &&
              accepted.root_height == 960 &&
              accepted.primary_output == initial.outputs[1].output_id,
          "tool is acknowledged only after policy and compositor commit");
  auto probe = launch(argv[4], {"--socket-dir", x11_dir, "--display", "91",
                                "--basic"});
  require(probe.wait_success(),
          "post-configuration lifecycle snapshot remains compositor-valid");
  const auto promoted = query(tool.get(), 103);
  require(promoted.result == GWIPC_OUTPUT_CONFIGURATION_ACCEPTED &&
              promoted.generation == 2 && promoted.outputs.size() == 2 &&
              promoted.outputs[1].logical_x == 0 &&
              promoted.outputs[1].logical_y == 480,
          "subsequent query observes the atomically promoted layout");

  tool.reset();
  server.stop();
  compositor_process.stop();
  wm_process.stop();
  std::filesystem::remove_all(directory);
  return 0;
}
