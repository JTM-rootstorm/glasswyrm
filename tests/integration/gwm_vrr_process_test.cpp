#include "ipc/vrr_membership_hint.hpp"

#include <glasswyrm/ipc.h>

#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

namespace {

[[noreturn]] void fail(const char* message) {
  std::fprintf(stderr, "gwm VRR process test: %s\n", message);
  std::exit(1);
}
void require(const bool condition, const char* message) {
  if (!condition) fail(message);
}

struct ConnectionDelete {
  void operator()(gwipc_connection* value) const {
    gwipc_connection_destroy(value);
  }
};
struct MessageDelete {
  void operator()(gwipc_message* value) const { gwipc_message_destroy(value); }
};
struct ContractPayloadDelete {
  void operator()(gwipc_contract_payload* value) const {
    gwipc_contract_payload_destroy(value);
  }
};
struct ControlPayloadDelete {
  void operator()(gwipc_control_payload* value) const {
    gwipc_control_payload_destroy(value);
  }
};
struct ContractDelete {
  void operator()(gwipc_decoded_contract* value) const {
    gwipc_decoded_contract_destroy(value);
  }
};
struct ControlDelete {
  void operator()(gwipc_decoded_control* value) const {
    gwipc_decoded_control_destroy(value);
  }
};

using Connection = std::unique_ptr<gwipc_connection, ConnectionDelete>;
using Message = std::unique_ptr<gwipc_message, MessageDelete>;

bool pump(gwipc_connection* connection, const int timeout = 50) {
  pollfd descriptor{gwipc_connection_fd(connection),
                    gwipc_connection_wanted_poll_events(connection), 0};
  const auto ready = ::poll(&descriptor, 1, timeout);
  if (ready < 0) return errno == EINTR;
  if (ready != 0 &&
      gwipc_connection_process_poll_events(connection, descriptor.revents) ==
          GWIPC_STATUS_SYSTEM_ERROR)
    return false;
  return gwipc_connection_get_state(connection) != GWIPC_CONNECTION_CLOSED;
}

Connection connect_to(const std::string& socket) {
  constexpr auto capabilities =
      GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_WINDOW_POLICY |
      GWIPC_CAP_WINDOW_LIFECYCLE | GWIPC_CAP_MULTI_OUTPUT_POLICY |
      GWIPC_CAP_SCALE_METADATA | GWIPC_CAP_VRR_POLICY;
  gwipc_connection_options options{};
  options.struct_size = sizeof(options);
  options.path = socket.c_str();
  options.local_role = GWIPC_ROLE_PROTOCOL_SERVER;
  options.acceptable_server_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_WINDOW_MANAGER);
  options.offered_capabilities = capabilities;
  options.required_peer_capabilities = capabilities;
  options.maximum_queued_bytes = GWIPC_HARD_MAXIMUM_QUEUED_BYTES;
  options.maximum_queued_messages = 8192;
  options.instance_label = "gwm-vrr-process-test";
  gwipc_connection* raw = nullptr;
  const auto status = gwipc_connection_connect(&options, &raw);
  require(status == GWIPC_STATUS_OK || status == GWIPC_STATUS_IN_PROGRESS,
          "connect producer");
  Connection connection(raw);
  for (int attempt = 0;
       attempt < 200 && gwipc_connection_get_state(connection.get()) !=
                            GWIPC_CONNECTION_ESTABLISHED;
       ++attempt)
    require(pump(connection.get()), "drive producer handshake");
  require(gwipc_connection_get_state(connection.get()) ==
              GWIPC_CONNECTION_ESTABLISHED,
          "M14 producer handshake establishes");
  return connection;
}

template <class Value, class Encoder>
void send_contract(gwipc_connection* connection, const std::uint16_t type,
                   const std::uint32_t flags, const Value& value,
                   Encoder encoder) {
  gwipc_contract_payload* raw = nullptr;
  require(encoder(&value, &raw) == GWIPC_STATUS_OK, "encode contract");
  const std::unique_ptr<gwipc_contract_payload, ContractPayloadDelete> payload(
      raw);
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
void send_control(gwipc_connection* connection, const std::uint16_t type,
                  const Value& value, Encoder encoder) {
  gwipc_control_payload* raw = nullptr;
  require(encoder(&value, &raw) == GWIPC_STATUS_OK, "encode control");
  const std::unique_ptr<gwipc_control_payload, ControlPayloadDelete> payload(
      raw);
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

gwipc_policy_output_upsert output(const std::uint64_t id,
                                  const std::int32_t x,
                                  const bool primary) {
  gwipc_policy_output_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = id;
  value.logical_x = x;
  value.logical_width = 800;
  value.logical_height = 600;
  value.work_x = x;
  value.work_width = 800;
  value.work_height = 600;
  value.scale_numerator = 1;
  value.scale_denominator = 1;
  value.transform = GWIPC_TRANSFORM_NORMAL;
  value.enabled = 1;
  value.primary = primary;
  return value;
}

void send_policy(gwipc_connection* connection) {
  gwipc_snapshot_begin begin{};
  begin.struct_size = sizeof(begin);
  begin.snapshot_id = 100;
  begin.domain = GWIPC_SNAPSHOT_WINDOW_POLICY;
  begin.generation = 7;
  begin.expected_item_count = 8;
  send_control(connection, GWIPC_MESSAGE_SNAPSHOT_BEGIN, begin,
               gwipc_control_encode_snapshot_begin);

  gwipc_policy_context_upsert context{};
  context.struct_size = sizeof(context);
  context.root_window_id = 1;
  context.workspace_id = 1;
  context.output_id = 10;
  context.work_width = 1600;
  context.work_height = 600;
  send_contract(connection, GWIPC_MESSAGE_POLICY_CONTEXT_UPSERT,
                GWIPC_FLAG_SNAPSHOT_ITEM, context,
                gwipc_contract_encode_policy_context_upsert);

  const auto right = output(20, 800, false);
  const auto left = output(10, 0, true);
  send_contract(connection, GWIPC_MESSAGE_POLICY_OUTPUT_UPSERT,
                GWIPC_FLAG_SNAPSHOT_ITEM, right,
                gwipc_contract_encode_policy_output_upsert);
  send_contract(connection, GWIPC_MESSAGE_POLICY_OUTPUT_UPSERT,
                GWIPC_FLAG_SNAPSHOT_ITEM, left,
                gwipc_contract_encode_policy_output_upsert);

  gwipc_policy_lifecycle_window_upsert window{};
  window.struct_size = sizeof(window);
  window.window.struct_size = sizeof(window.window);
  window.window.window_id = 1001;
  window.window.parent_window_id = 1;
  window.window.workspace_id = 1;
  window.window.requested_width = 800;
  window.window.requested_height = 600;
  window.window.window_type = GWIPC_POLICY_WINDOW_NORMAL;
  window.window.map_intent = GWIPC_POLICY_WANTS_MAP;
  window.window.decoration_preference = GWIPC_TRI_STATE_FALSE;
  window.window.creation_serial = 1;
  window.window.map_serial = 1;
  window.window.focus_serial = 1;
  window.geometry_serial = 1;
  send_contract(connection, GWIPC_MESSAGE_POLICY_LIFECYCLE_WINDOW_UPSERT,
                GWIPC_FLAG_SNAPSHOT_ITEM, window,
                gwipc_contract_encode_policy_lifecycle_window_upsert);

  const std::array output_ids{UINT64_C(10), UINT64_C(20)};
  const std::array membership{UINT64_C(10), UINT64_C(20)};
  const auto encoded =
      glasswyrm::ipc::internal::encode_vrr_membership_hint(output_ids,
                                                           membership);
  require(encoded.has_value(), "encode exact spanning membership");
  gwipc_policy_window_output_hint hint{};
  hint.struct_size = sizeof(hint);
  hint.window_id = 1001;
  hint.previous_output_id = 10;
  hint.preferred_output_id = *encoded;
  send_contract(connection, GWIPC_MESSAGE_POLICY_WINDOW_OUTPUT_HINT,
                GWIPC_FLAG_SNAPSHOT_ITEM, hint,
                gwipc_contract_encode_policy_window_output_hint);

  gwipc_policy_output_vrr_upsert left_vrr{};
  left_vrr.struct_size = sizeof(left_vrr);
  left_vrr.output_id = 10;
  left_vrr.mode = GWIPC_VRR_POLICY_FOCUSED;
  left_vrr.hardware_capable = 1;
  left_vrr.kms_controllable = 1;
  auto right_vrr = left_vrr;
  right_vrr.output_id = 20;
  right_vrr.mode = GWIPC_VRR_POLICY_OFF;
  send_contract(connection, GWIPC_MESSAGE_POLICY_OUTPUT_VRR_UPSERT,
                GWIPC_FLAG_SNAPSHOT_ITEM, right_vrr,
                gwipc_contract_encode_policy_output_vrr_upsert);
  send_contract(connection, GWIPC_MESSAGE_POLICY_OUTPUT_VRR_UPSERT,
                GWIPC_FLAG_SNAPSHOT_ITEM, left_vrr,
                gwipc_contract_encode_policy_output_vrr_upsert);

  gwipc_policy_window_vrr_upsert window_vrr{};
  window_vrr.struct_size = sizeof(window_vrr);
  window_vrr.window_id = 1001;
  window_vrr.preference = GWIPC_VRR_PREFERENCE_PREFER;
  send_contract(connection, GWIPC_MESSAGE_POLICY_WINDOW_VRR_UPSERT,
                GWIPC_FLAG_SNAPSHOT_ITEM, window_vrr,
                gwipc_contract_encode_policy_window_vrr_upsert);

  gwipc_snapshot_end end{};
  end.struct_size = sizeof(end);
  end.snapshot_id = 100;
  end.generation = 7;
  end.actual_item_count = 8;
  send_control(connection, GWIPC_MESSAGE_SNAPSHOT_END, end,
               gwipc_control_encode_snapshot_end);

  gwipc_policy_commit commit{};
  commit.struct_size = sizeof(commit);
  commit.commit_id = 100;
  commit.producer_generation = 7;
  send_contract(connection, GWIPC_MESSAGE_POLICY_COMMIT,
                GWIPC_FLAG_ACK_REQUIRED, commit,
                gwipc_contract_encode_policy_commit);
}

void receive_policy(gwipc_connection* connection) {
  constexpr std::array expected{
      std::uint16_t{GWIPC_MESSAGE_SNAPSHOT_BEGIN},
      std::uint16_t{GWIPC_MESSAGE_POLICY_WINDOW_STATE},
      std::uint16_t{GWIPC_MESSAGE_POLICY_OUTPUT_VRR_STATE},
      std::uint16_t{GWIPC_MESSAGE_POLICY_OUTPUT_VRR_STATE},
      std::uint16_t{GWIPC_MESSAGE_POLICY_WINDOW_VRR_STATE},
      std::uint16_t{GWIPC_MESSAGE_SNAPSHOT_END},
      std::uint16_t{GWIPC_MESSAGE_POLICY_ACKNOWLEDGED}};
  std::size_t received = 0;
  for (int attempt = 0; attempt < 300 && received < expected.size();
       ++attempt) {
    require(pump(connection), "drive M14 policy response");
    for (;;) {
      gwipc_message* raw = nullptr;
      if (gwipc_connection_receive(connection, &raw) != GWIPC_STATUS_OK) break;
      const Message message(raw);
      require(gwipc_message_type(message.get()) == expected[received],
              "M14 response ordering is deterministic");
      const auto type = expected[received];
      if (type == GWIPC_MESSAGE_SNAPSHOT_BEGIN ||
          type == GWIPC_MESSAGE_SNAPSHOT_END) {
        gwipc_decoded_control* decoded_raw = nullptr;
        require(gwipc_control_decode_message(message.get(), &decoded_raw) ==
                    GWIPC_STATUS_OK,
                "decode snapshot control");
        const std::unique_ptr<gwipc_decoded_control, ControlDelete> decoded(
            decoded_raw);
        if (type == GWIPC_MESSAGE_SNAPSHOT_BEGIN) {
          const auto* value = gwipc_decoded_snapshot_begin(decoded.get());
          require(value && value->generation == 7 &&
                      value->expected_item_count == 4,
                  "M14 reply advertises base plus exact VRR result count");
        }
      } else {
        gwipc_decoded_contract* decoded_raw = nullptr;
        require(gwipc_contract_decode_message(message.get(), &decoded_raw) ==
                    GWIPC_STATUS_OK,
                "decode M14 result");
        const std::unique_ptr<gwipc_decoded_contract, ContractDelete> decoded(
            decoded_raw);
        if (received == 2 || received == 3) {
          const auto* value =
              gwipc_decoded_policy_output_vrr_state(decoded.get());
          require(value && value->output_id == (received == 2 ? 10U : 20U),
                  "VRR output results use canonical output-ID order");
          if (received == 2)
            require(value->mode == GWIPC_VRR_POLICY_FOCUSED &&
                        value->selected_window_id == 0 &&
                        !value->desired_enabled && value->candidate_required &&
                        value->reason_flags == GWIPC_VRR_REASON_NO_CANDIDATE,
                    "spanning membership prevents focused selection");
        } else if (received == 4) {
          const auto* value =
              gwipc_decoded_policy_window_vrr_state(decoded.get());
          require(value && value->window_id == 1001 && value->output_id == 10 &&
                      value->preference == GWIPC_VRR_PREFERENCE_PREFER &&
                      value->focused && !value->eligible && !value->selected &&
                      !value->exclusive_output_membership &&
                      value->reason_flags ==
                          GWIPC_VRR_REASON_WINDOW_SPANS_OUTPUTS,
                  "running gwm consumes exact membership and emits reasons");
        } else if (received == 6) {
          const auto* value =
              gwipc_decoded_policy_acknowledged(decoded.get());
          require(value && value->result == GWIPC_POLICY_ACCEPTED &&
                      value->applied_generation == 7 &&
                      value->window_count == 1 && value->policy_hash != 0,
                  "running gwm acknowledges the v4 policy hash");
          require(value->policy_hash == UINT64_C(0x92fcb6b91276e7ac),
                  "running gwm v4 hash matches its frozen wire vector");
        }
      }
      ++received;
    }
  }
  require(received == expected.size(), "complete M14 response arrives");
}

}  // namespace

int main(int argc, char** argv) {
  require(argc == 2, "usage: gwm_vrr_process_test /path/to/gwm");
  char temporary[] = "/tmp/gwm-vrr-process-test-XXXXXX";
  require(::mkdtemp(temporary) != nullptr, "create temporary directory");
  const std::filesystem::path root = temporary;
  const auto socket = (root / "gwm.sock").string();
  const auto child = ::fork();
  require(child >= 0, "fork gwm");
  if (child == 0) {
    ::execl(argv[1], argv[1], "--ipc-socket", socket.c_str(),
            "--max-commits", "1", nullptr);
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
  auto connection = connect_to(socket);
  send_policy(connection.get());
  receive_policy(connection.get());
  connection.reset();
  int child_status = 0;
  require(::waitpid(child, &child_status, 0) == child, "wait for gwm");
  require(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0,
          "gwm exits after accepted M14 commit");
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return 0;
}
