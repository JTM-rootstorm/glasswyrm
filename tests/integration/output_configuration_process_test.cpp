#include <glasswyrm/ipc.h>

#include "config.hpp"
#include "protocol/x11/core.hpp"
#include "tests/helpers/x11_fake_client.hpp"
#include "tests/helpers/x11_request_builder.hpp"
#include "tests/helpers/test_support.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <memory>
#include <poll.h>
#include <system_error>
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
namespace x11 = gw::protocol::x11;

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
    return wait_exit_code(0);
  }
  bool wait_exit_code(const int expected) noexcept {
    if (pid <= 0)
      return false;
    int status = 0;
    const auto result = ::waitpid(pid, &status, 0);
    pid = -1;
    return result > 0 && WIFEXITED(status) &&
           WEXITSTATUS(status) == expected;
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

std::size_t descriptor_count(const pid_t process) {
  std::error_code error;
  std::size_t count = 0;
  const auto directory =
      std::filesystem::path("/proc") / std::to_string(process) / "fd";
  for (std::filesystem::directory_iterator entries(directory, error), end;
       !error && entries != end; entries.increment(error))
    ++count;
  require(!error, "inspect server descriptor count");
  return count;
}

void wait_for_descriptor_bound(const pid_t process,
                               const std::size_t maximum) {
  constexpr unsigned kAttempts = 500;
  constexpr auto kInterval = std::chrono::milliseconds(10);
  std::size_t observed = 0;
  for (unsigned attempt = 0; attempt < kAttempts; ++attempt) {
    observed = descriptor_count(process);
    if (observed <= maximum)
      return;
    std::this_thread::sleep_for(kInterval);
  }
  observed = descriptor_count(process);
  if (observed <= maximum)
    return;
  require(false,
          "descriptor cleanup timeout: expected at most " +
              std::to_string(maximum) + ", observed " +
              std::to_string(observed) + " after " +
              std::to_string(kAttempts * kInterval.count()) + " ms for pid " +
              std::to_string(process));
}

class PersistentX11Client final {
 public:
  explicit PersistentX11Client(const std::string& socket)
      : client_(socket), wire_(x11::ByteOrder::LittleEndian) {
    client_.send_all(gw::test::make_setup_request(order_));
    const auto setup = client_.receive_setup_reply(order_);
    require(setup.size() >= 16 && setup[0] == 1,
            "persistent X11 client establishes before output change");
    window_ = gw::test::read_wire_u32(setup.data() + 12, order_) + 1U;
    client_.send_all(wire_.create_window(window_, 1, 12, 14, 96, 72));
    const std::array<std::uint8_t, 4> window_bytes{
        static_cast<std::uint8_t>(window_),
        static_cast<std::uint8_t>(window_ >> 8U),
        static_cast<std::uint8_t>(window_ >> 16U),
        static_cast<std::uint8_t>(window_ >> 24U)};
    client_.send_all(wire_.raw(
        static_cast<std::uint8_t>(x11::CoreOpcode::MapWindow), 0,
        window_bytes));
    synchronize();
    require_usable();
  }

  void require_usable() {
    client_.send_all(wire_.get_geometry(window_));
    const auto geometry = client_.receive_server_packet(order_);
    require(!geometry.empty() && geometry[0] == 1,
            "persistent X11 window remains queryable");
    synchronize();
  }

  void begin_unmap() {
    const std::array<std::uint8_t, 4> window_bytes{
        static_cast<std::uint8_t>(window_),
        static_cast<std::uint8_t>(window_ >> 8U),
        static_cast<std::uint8_t>(window_ >> 16U),
        static_cast<std::uint8_t>(window_ >> 24U)};
    client_.send_all(wire_.raw(
        static_cast<std::uint8_t>(x11::CoreOpcode::UnmapWindow), 0,
        window_bytes));
  }

 private:
  void synchronize() {
    client_.send_all(wire_.get_input_focus());
    const auto reply = client_.receive_server_packet(order_);
    require(!reply.empty() && reply[0] == 1,
            "persistent X11 client remains synchronized");
  }

  static constexpr auto order_ = x11::ByteOrder::LittleEndian;
  gw::test::X11FakeClient client_;
  gw::test::X11RequestBuilder wire_;
  std::uint32_t window_{};
};

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

Connection connect_tool(const std::string& path, const bool vrr = false) {
  gwipc_connection_options options{};
  options.struct_size = sizeof(options);
  options.path = path.c_str();
  options.local_role = GWIPC_ROLE_DIAGNOSTIC_TOOL;
  options.acceptable_server_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_PROTOCOL_SERVER);
  options.offered_capabilities =
      GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE | GWIPC_CAP_OUTPUT_CONTROL;
  if (vrr)
    options.offered_capabilities |= GWIPC_CAP_VRR_METADATA |
                                    GWIPC_CAP_VRR_POLICY |
                                    GWIPC_CAP_PRESENTATION_TIMING;
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
  std::vector<std::uint64_t> vrr_capabilities;
  std::vector<std::uint64_t> vrr_policies;
  std::vector<std::uint64_t> vrr_states;
};

Reply receive_reply(gwipc_connection* connection,
                    const unsigned maximum_attempts = 800) {
  Reply reply;
  for (unsigned attempt = 0; attempt < maximum_attempts; ++attempt) {
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
      if (gwipc_message_type(message.get()) ==
          GWIPC_MESSAGE_OUTPUT_VRR_CAPABILITY_UPSERT) {
        gwipc_decoded_contract* decoded_raw = nullptr;
        require(gwipc_contract_decode_message(message.get(), &decoded_raw) ==
                    GWIPC_STATUS_OK,
                "decode queried VRR capability");
        std::unique_ptr<gwipc_decoded_contract,
                        decltype(&gwipc_decoded_contract_destroy)>
            decoded(decoded_raw, gwipc_decoded_contract_destroy);
        const auto* value =
            gwipc_decoded_output_vrr_capability_upsert(decoded.get());
        require(value != nullptr, "queried VRR capability is typed");
        reply.vrr_capabilities.push_back(value->output_id);
      }
      if (gwipc_message_type(message.get()) ==
          GWIPC_MESSAGE_OUTPUT_VRR_POLICY_UPSERT) {
        gwipc_decoded_contract* decoded_raw = nullptr;
        require(gwipc_contract_decode_message(message.get(), &decoded_raw) ==
                    GWIPC_STATUS_OK,
                "decode queried VRR policy");
        std::unique_ptr<gwipc_decoded_contract,
                        decltype(&gwipc_decoded_contract_destroy)>
            decoded(decoded_raw, gwipc_decoded_contract_destroy);
        const auto* value =
            gwipc_decoded_output_vrr_policy_upsert(decoded.get());
        require(value != nullptr, "queried VRR policy is typed");
        reply.vrr_policies.push_back(value->output_id);
      }
      if (gwipc_message_type(message.get()) ==
          GWIPC_MESSAGE_OUTPUT_VRR_STATE_UPSERT) {
        gwipc_decoded_contract* decoded_raw = nullptr;
        require(gwipc_contract_decode_message(message.get(), &decoded_raw) ==
                    GWIPC_STATUS_OK,
                "decode queried VRR state");
        std::unique_ptr<gwipc_decoded_contract,
                        decltype(&gwipc_decoded_contract_destroy)>
            decoded(decoded_raw, gwipc_decoded_contract_destroy);
        const auto* value =
            gwipc_decoded_output_vrr_state_upsert(decoded.get());
        require(value != nullptr, "queried VRR state is typed");
        reply.vrr_states.push_back(value->output_id);
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

Reply query(gwipc_connection* connection, const std::uint64_t id,
            const bool vrr = false) {
  gwipc_output_state_query query{};
  query.struct_size = sizeof(query);
  query.query_id = id;
  query.flags = GWIPC_OUTPUT_QUERY_LAYOUT;
  if (vrr)
    query.flags |= GWIPC_OUTPUT_QUERY_VRR;
  send_contract(connection, GWIPC_MESSAGE_OUTPUT_STATE_QUERY,
                GWIPC_FLAG_ACK_REQUIRED, query,
                gwipc_contract_encode_output_state_query);
  return receive_reply(connection);
}

void send_configuration(gwipc_connection* connection, const std::uint64_t id,
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
}

Reply configure(gwipc_connection* connection, const std::uint64_t id,
                const Reply& initial) {
  send_configuration(connection, id, initial);
  return receive_reply(connection);
}

} // namespace

int main(int argc, char** argv) {
  require(argc == 6,
          "expected glasswyrmd, gwm, gwcomp, X11 probe, and rejecting "
          "compositor peer paths");
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
  PersistentX11Client persistent(x11_dir + "/X91");
  require(::kill(server.pid, SIGSTOP) == 0,
          "pause server before queuing concurrent work");
  int stop_status = 0;
  require(::waitpid(server.pid, &stop_status, WUNTRACED) == server.pid &&
              WIFSTOPPED(stop_status),
          "server reaches the stopped state");
  persistent.begin_unmap();
  send_configuration(tool.get(), 102, initial);
  for (unsigned attempt = 0; attempt < 4; ++attempt)
    require(pump(tool.get()), "flush queued output configuration");
  require(::kill(server.pid, SIGCONT) == 0,
          "resume server with concurrent work queued");
  const auto accepted = receive_reply(tool.get(), 100);
  require(accepted.result == GWIPC_OUTPUT_CONFIGURATION_ACCEPTED &&
              accepted.generation == 2 && accepted.root_width == 640 &&
              accepted.root_height == 960 &&
              accepted.primary_output == initial.outputs[1].output_id,
          "queued output work starts when the preceding lifecycle completes");
  persistent.require_usable();
  auto historical_probe =
      launch(argv[4], {"--socket-dir", x11_dir, "--display", "91", "--basic"});
  require(historical_probe.wait_success(),
          "post-configuration lifecycle snapshot remains compositor-valid");
  auto geometry_probe = launch(argv[4],
                               {"--socket-dir", x11_dir, "--display", "91",
                                "--expect-root", "640x960", "--basic"});
  require(geometry_probe.wait_success(),
          "new X11 clients observe the committed root dimensions");
  auto stale_geometry_probe =
      launch(argv[4], {"--socket-dir", x11_dir, "--display", "91",
                       "--expect-root", "1280x480", "--basic"});
  require(stale_geometry_probe.wait_exit_code(1),
          "the X11 probe rejects stale root dimensions");
  const auto promoted = query(tool.get(), 103);
  require(promoted.result == GWIPC_OUTPUT_CONFIGURATION_ACCEPTED &&
              promoted.generation == 2 && promoted.outputs.size() == 2 &&
              promoted.outputs[1].logical_x == 0 &&
              promoted.outputs[1].logical_y == 480,
          "subsequent query observes the atomically promoted layout");

  const auto descriptor_baseline = descriptor_count(server.pid);
  for (std::uint64_t repetition = 0; repetition < 32; ++repetition) {
    auto repeated = connect_tool(control);
    const auto snapshot = query(repeated.get(), 200 + repetition);
    require(snapshot.result == GWIPC_OUTPUT_CONFIGURATION_ACCEPTED &&
                snapshot.generation == 2 && snapshot.outputs.size() == 2,
            "repeated control peer receives a complete current snapshot");
  }
  wait_for_descriptor_bound(server.pid, descriptor_baseline);
  const auto after_repetition = query(tool.get(), 300);
  require(after_repetition.result == GWIPC_OUTPUT_CONFIGURATION_ACCEPTED &&
              after_repetition.generation == 2 &&
              after_repetition.outputs.size() == 2,
          "healthy control peer remains usable after bounded repetition");

  tool.reset();
  server.stop();
  compositor_process.stop();
  wm_process.stop();
  std::filesystem::remove_all(directory);

#if GW_HAS_EXPERIMENTAL
  std::string vrr_directory = "/tmp/glasswyrm-output-vrr-repeat-XXXXXX";
  require(::mkdtemp(vrr_directory.data()) != nullptr,
          "create VRR repetition directory");
  const auto vrr_wm = vrr_directory + "/gwm.sock";
  const auto vrr_compositor = vrr_directory + "/gwcomp.sock";
  const auto vrr_control = vrr_directory + "/control.sock";
  const auto vrr_x11 = vrr_directory + "/x11";
  const auto vrr_dumps = vrr_directory + "/dumps";
  std::filesystem::create_directories(vrr_x11);
  std::filesystem::create_directories(vrr_dumps);
  auto vrr_wm_process = launch(argv[2], {"--ipc-socket", vrr_wm});
  auto vrr_compositor_process = launch(
      argv[3], {"--backend", "headless", "--ipc-socket", vrr_compositor,
                "--dump-dir", vrr_dumps, "--headless-output",
                "LEFT:640x480@60000", "--headless-output",
                "RIGHT:640x480@60000", "--headless-vrr",
                "LEFT=40000-60000", "--headless-vrr",
                "RIGHT=40000-60000"});
  wait_for_socket(vrr_wm);
  wait_for_socket(vrr_compositor);
  auto vrr_server = launch(
      argv[1], {"--display", "93", "--socket-dir", vrr_x11,
                "--wm-socket", vrr_wm, "--compositor-socket", vrr_compositor,
                "--output-model", "--control-socket", vrr_control,
                "--software-content", "--vrr-protocol"});
  wait_for_socket(vrr_control);
  const auto vrr_descriptor_baseline = descriptor_count(vrr_server.pid);
  for (std::uint64_t repetition = 0; repetition < 32; ++repetition) {
    auto repeated = connect_tool(vrr_control, true);
    const auto snapshot = query(repeated.get(), 500 + repetition, true);
    require(snapshot.result == GWIPC_OUTPUT_CONFIGURATION_ACCEPTED &&
                snapshot.generation == 1 && snapshot.outputs.size() == 2 &&
                snapshot.vrr_capabilities.size() == 2 &&
                snapshot.vrr_policies.size() == 2 &&
                snapshot.vrr_states.size() == 2,
            "repeated M14 control peer receives complete VRR state");
  }
  wait_for_descriptor_bound(vrr_server.pid, vrr_descriptor_baseline);
  auto vrr_tool = connect_tool(vrr_control, true);
  const auto vrr_after_repetition = query(vrr_tool.get(), 600, true);
  require(vrr_after_repetition.result == GWIPC_OUTPUT_CONFIGURATION_ACCEPTED &&
              vrr_after_repetition.vrr_states.size() == 2,
          "M14 control remains usable after negotiated disconnect repetition");
  vrr_tool.reset();
  vrr_server.stop();
  vrr_compositor_process.stop();
  vrr_wm_process.stop();
  std::filesystem::remove_all(vrr_directory);
#endif

  std::string rollback_directory =
      "/tmp/glasswyrm-output-rollback-XXXXXX";
  require(::mkdtemp(rollback_directory.data()) != nullptr,
          "create output rollback directory");
  const auto rollback_wm = rollback_directory + "/gwm.sock";
  const auto rollback_compositor = rollback_directory + "/gwcomp.sock";
  const auto rollback_control = rollback_directory + "/control.sock";
  const auto rollback_x11 = rollback_directory + "/x11";
  std::filesystem::create_directories(rollback_x11);

  auto rollback_wm_process = launch(argv[2], {"--ipc-socket", rollback_wm});
  auto rejecting_compositor = launch(argv[5], {rollback_compositor});
  wait_for_socket(rollback_wm);
  wait_for_socket(rollback_compositor);
  auto rollback_server =
      launch(argv[1], {"--display", "92", "--socket-dir", rollback_x11,
                       "--wm-socket", rollback_wm, "--compositor-socket",
                       rollback_compositor, "--output-model",
                       "--control-socket", rollback_control});
  wait_for_socket(rollback_control);

  auto rollback_tool = connect_tool(rollback_control);
  const auto before_rejection = query(rollback_tool.get(), 401);
  require(before_rejection.result == GWIPC_OUTPUT_CONFIGURATION_ACCEPTED &&
              before_rejection.generation == 1 &&
              before_rejection.root_width == 1280 &&
              before_rejection.root_height == 480,
          "rollback fixture begins with the compositor inventory layout");
  const auto rejected = configure(rollback_tool.get(), 402, before_rejection);
  require(rejected.result == GWIPC_OUTPUT_CONFIGURATION_COMPOSITOR_REJECTED &&
              rejected.generation == 1 && rejected.root_width == 1280 &&
              rejected.root_height == 480 &&
              rejected.primary_output == before_rejection.primary_output,
          "compositor frame rejection reports the retained layout");
  const auto after_rejection = query(rollback_tool.get(), 403);
  require(after_rejection.result == GWIPC_OUTPUT_CONFIGURATION_ACCEPTED &&
              after_rejection.generation == 1 &&
              after_rejection.root_width == 1280 &&
              after_rejection.root_height == 480 &&
              after_rejection.primary_output ==
                  before_rejection.primary_output &&
              after_rejection.outputs.size() == 2 &&
              after_rejection.outputs[1].logical_x == 640 &&
              after_rejection.outputs[1].logical_y == 0,
          "server and GWM rollback preserve the old output policy");
  const auto recovered = configure(rollback_tool.get(), 404, after_rejection);
  require(recovered.result == GWIPC_OUTPUT_CONFIGURATION_ACCEPTED &&
              recovered.generation == 2 && recovered.root_width == 640 &&
              recovered.root_height == 960,
          "the split processes accept a later transaction after rollback");

  rollback_tool.reset();
  rollback_server.stop();
  rejecting_compositor.stop();
  rollback_wm_process.stop();
  std::filesystem::remove_all(rollback_directory);
  return 0;
}
