#include <glasswyrm/ipc.h>

#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

pid_t child_process = -1;

[[noreturn]] void fail(const char *message) {
  if (child_process > 0) {
    (void)::kill(child_process, SIGKILL);
    (void)::waitpid(child_process, nullptr, 0);
  }
  std::fprintf(stderr, "gwcomp output inventory process test: %s\n", message);
  std::exit(1);
}

void require(const bool value, const char *message) {
  if (!value) fail(message);
}

struct ConnectionDeleter {
  void operator()(gwipc_connection *value) const {
    gwipc_connection_destroy(value);
  }
};

struct MessageDeleter {
  void operator()(gwipc_message *value) const { gwipc_message_destroy(value); }
};

struct ContractPayloadDeleter {
  void operator()(gwipc_contract_payload *value) const {
    gwipc_contract_payload_destroy(value);
  }
};

struct ControlPayloadDeleter {
  void operator()(gwipc_control_payload *value) const {
    gwipc_control_payload_destroy(value);
  }
};

struct DecodedContractDeleter {
  void operator()(gwipc_decoded_contract *value) const {
    gwipc_decoded_contract_destroy(value);
  }
};

struct DecodedControlDeleter {
  void operator()(gwipc_decoded_control *value) const {
    gwipc_decoded_control_destroy(value);
  }
};

using Connection = std::unique_ptr<gwipc_connection, ConnectionDeleter>;
using Message = std::unique_ptr<gwipc_message, MessageDeleter>;
using ContractPayload =
    std::unique_ptr<gwipc_contract_payload, ContractPayloadDeleter>;
using ControlPayload =
    std::unique_ptr<gwipc_control_payload, ControlPayloadDeleter>;
using DecodedContract =
    std::unique_ptr<gwipc_decoded_contract, DecodedContractDeleter>;
using DecodedControl =
    std::unique_ptr<gwipc_decoded_control, DecodedControlDeleter>;

bool pump(gwipc_connection *connection) {
  pollfd descriptor{gwipc_connection_fd(connection),
                    gwipc_connection_wanted_poll_events(connection), 0};
  const int ready = ::poll(&descriptor, 1, 50);
  if (ready < 0) return errno == EINTR;
  if (ready > 0)
    (void)gwipc_connection_process_poll_events(connection,
                                               descriptor.revents);
  return gwipc_connection_get_state(connection) != GWIPC_CONNECTION_CLOSED;
}

Connection connect_to(const std::string &path) {
  constexpr std::uint64_t capabilities =
      GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE |
      GWIPC_CAP_SURFACE_STATE | GWIPC_CAP_SDR_COLOR_METADATA |
      GWIPC_CAP_FRAME_ACKNOWLEDGEMENT | GWIPC_CAP_WINDOW_LIFECYCLE |
      GWIPC_CAP_OUTPUT_MANAGEMENT | GWIPC_CAP_SURFACE_OUTPUT_MEMBERSHIP |
      GWIPC_CAP_SCALE_METADATA;
  gwipc_connection_options options{};
  options.struct_size = sizeof(options);
  options.path = path.c_str();
  options.local_role = GWIPC_ROLE_PROTOCOL_SERVER;
  options.acceptable_server_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_COMPOSITOR);
  options.offered_capabilities = capabilities;
  options.required_peer_capabilities = capabilities;
  options.instance_label = "gwcomp-output-inventory-process-test";
  gwipc_connection *raw = nullptr;
  const auto status = gwipc_connection_connect(&options, &raw);
  require(status == GWIPC_STATUS_OK || status == GWIPC_STATUS_IN_PROGRESS,
          "connect ProtocolServer");
  Connection connection(raw);
  for (int attempt = 0;
       attempt < 200 && gwipc_connection_get_state(connection.get()) !=
                            GWIPC_CONNECTION_ESTABLISHED;
       ++attempt)
    require(pump(connection.get()), "drive ProtocolServer handshake");
  require(gwipc_connection_get_state(connection.get()) ==
              GWIPC_CONNECTION_ESTABLISHED,
          "ProtocolServer establishes with M13 output capabilities");
  return connection;
}

void require_historical_peer_rejected(const std::string &path) {
  gwipc_connection_options options{};
  options.struct_size = sizeof(options);
  options.path = path.c_str();
  options.local_role = GWIPC_ROLE_PROTOCOL_SERVER;
  options.acceptable_server_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_COMPOSITOR);
  options.offered_capabilities =
      GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE |
      GWIPC_CAP_SURFACE_STATE | GWIPC_CAP_SDR_COLOR_METADATA |
      GWIPC_CAP_FRAME_ACKNOWLEDGEMENT | GWIPC_CAP_WINDOW_LIFECYCLE;
  options.instance_label = "gwcomp-historical-output-process-test";
  gwipc_connection *raw = nullptr;
  const auto status = gwipc_connection_connect(&options, &raw);
  require(status == GWIPC_STATUS_OK || status == GWIPC_STATUS_IN_PROGRESS,
          "connect historical ProtocolServer to explicit output layout");
  Connection connection(raw);
  for (int attempt = 0;
       attempt < 200 && gwipc_connection_get_state(connection.get()) !=
                            GWIPC_CONNECTION_CLOSED;
       ++attempt) {
    pollfd descriptor{gwipc_connection_fd(connection.get()),
                      gwipc_connection_wanted_poll_events(connection.get()),
                      0};
    const int ready = ::poll(&descriptor, 1, 20);
    if (ready > 0)
      (void)gwipc_connection_process_poll_events(connection.get(),
                                                 descriptor.revents);
  }
  require(gwipc_connection_get_state(connection.get()) ==
              GWIPC_CONNECTION_CLOSED,
          "explicit output layout rejects a historical ProtocolServer");
}

std::uint64_t send_query(gwipc_connection *connection) {
  gwipc_output_state_query query{};
  query.struct_size = sizeof(query);
  query.query_id = 73;
  query.flags = GWIPC_OUTPUT_QUERY_DESCRIPTORS | GWIPC_OUTPUT_QUERY_MODES |
                GWIPC_OUTPUT_QUERY_LAYOUT;
  gwipc_contract_payload *raw = nullptr;
  require(gwipc_contract_encode_output_state_query(&query, &raw) ==
              GWIPC_STATUS_OK,
          "encode output state query");
  ContractPayload payload(raw);
  std::size_t size = 0;
  const auto *bytes = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = GWIPC_MESSAGE_OUTPUT_STATE_QUERY;
  message.flags = GWIPC_FLAG_ACK_REQUIRED;
  message.payload = bytes;
  message.payload_size = size;
  std::uint64_t sequence = 0;
  require(gwipc_connection_enqueue_with_sequence(connection, &message,
                                                 &sequence) ==
              GWIPC_STATUS_OK,
          "enqueue output state query");
  require(sequence != 0, "output state query receives a wire sequence");
  return sequence;
}

struct InventoryRecord {
  std::uint16_t type{};
  std::vector<std::uint8_t> payload;

  friend bool operator==(const InventoryRecord &, const InventoryRecord &) =
      default;
};

struct InventoryReply {
  std::uint64_t snapshot_id{};
  std::uint64_t generation{};
  std::uint64_t primary_output_id{};
  std::uint32_t root_width{};
  std::uint32_t root_height{};
  std::vector<std::uint64_t> descriptor_ids;
  std::vector<std::string> names;
  std::vector<gwipc_output_mode_upsert> modes;
  std::vector<gwipc_output_upsert> states;
  std::vector<InventoryRecord> records;
};

DecodedContract decode_contract(const gwipc_message *message) {
  gwipc_decoded_contract *raw = nullptr;
  require(gwipc_contract_decode_message(message, &raw) == GWIPC_STATUS_OK,
          "decode inventory contract");
  return DecodedContract(raw);
}

InventoryReply receive_inventory(gwipc_connection *connection,
                                 const std::uint64_t query_sequence) {
  InventoryReply reply;
  bool saw_begin = false;
  bool saw_end = false;
  bool saw_acknowledgement = false;
  std::uint32_t expected_items = 0;
  std::uint32_t actual_items = 0;

  for (int attempt = 0; attempt < 300 && !saw_acknowledgement; ++attempt) {
    require(pump(connection), "drive output inventory reply");
    for (;;) {
      gwipc_message *raw = nullptr;
      if (gwipc_connection_receive(connection, &raw) != GWIPC_STATUS_OK)
        break;
      Message message(raw);
      const auto type = gwipc_message_type(message.get());
      if (type == GWIPC_MESSAGE_SNAPSHOT_BEGIN ||
          type == GWIPC_MESSAGE_SNAPSHOT_END) {
        gwipc_decoded_control *decoded_raw = nullptr;
        require(gwipc_control_decode_message(message.get(), &decoded_raw) ==
                    GWIPC_STATUS_OK,
                "decode output inventory control record");
        DecodedControl decoded(decoded_raw);
        if (type == GWIPC_MESSAGE_SNAPSHOT_BEGIN) {
          const auto *begin = gwipc_decoded_snapshot_begin(decoded.get());
          require(begin && !saw_begin &&
                      begin->domain == GWIPC_SNAPSHOT_OUTPUTS,
                  "receive one Outputs snapshot begin");
          saw_begin = true;
          reply.snapshot_id = begin->snapshot_id;
          reply.generation = begin->generation;
          expected_items = begin->expected_item_count;
        } else {
          const auto *end = gwipc_decoded_snapshot_end(decoded.get());
          require(end && saw_begin && !saw_end &&
                      end->snapshot_id == reply.snapshot_id &&
                      end->generation == reply.generation,
                  "receive matching Outputs snapshot end");
          saw_end = true;
          actual_items = end->actual_item_count;
        }
        continue;
      }

      auto decoded = decode_contract(message.get());
      if (type == GWIPC_MESSAGE_OUTPUT_DESCRIPTOR_UPSERT) {
        const auto *descriptor =
            gwipc_decoded_output_descriptor_upsert(decoded.get());
        require(descriptor && descriptor->name,
                "decode output descriptor record");
        reply.descriptor_ids.push_back(descriptor->output_id);
        reply.names.emplace_back(descriptor->name, descriptor->name_length);
      } else if (type == GWIPC_MESSAGE_OUTPUT_MODE_UPSERT) {
        const auto *mode = gwipc_decoded_output_mode_upsert(decoded.get());
        require(mode, "decode output mode record");
        reply.modes.push_back(*mode);
      } else if (type == GWIPC_MESSAGE_OUTPUT_UPSERT) {
        const auto *state = gwipc_decoded_output_upsert(decoded.get());
        require(state, "decode output layout record");
        reply.states.push_back(*state);
      } else if (type == GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED) {
        const auto *acknowledged =
            gwipc_decoded_output_configuration_acknowledged(decoded.get());
        require(acknowledged && saw_end &&
                    gwipc_message_flags(message.get()) == GWIPC_FLAG_REPLY &&
                    gwipc_message_reply_to(message.get()) == query_sequence &&
                    acknowledged->request_id == 73 &&
                    acknowledged->result ==
                        GWIPC_OUTPUT_CONFIGURATION_ACCEPTED &&
                    acknowledged->applied_generation == reply.generation &&
                    acknowledged->enabled_output_count == 2,
                "receive correlated accepted inventory acknowledgement");
        reply.primary_output_id = acknowledged->primary_output_id;
        reply.root_width = acknowledged->root_logical_width;
        reply.root_height = acknowledged->root_logical_height;
        saw_acknowledgement = true;
        continue;
      } else {
        fail("receive only output inventory records");
      }

      require(gwipc_message_flags(message.get()) == GWIPC_FLAG_SNAPSHOT_ITEM,
              "inventory data record is a snapshot item");
      std::size_t payload_size = 0;
      const auto *payload =
          gwipc_message_payload(message.get(), &payload_size);
      reply.records.push_back(
          {type, std::vector<std::uint8_t>(payload, payload + payload_size)});
    }
  }

  require(saw_begin && saw_end && saw_acknowledgement,
          "complete output inventory arrives before timeout");
  require(expected_items == 6 && actual_items == 6 &&
              reply.records.size() == 6,
          "two-output inventory contains two descriptors, modes, and states");
  return reply;
}

void validate_inventory(const InventoryReply &reply) {
  require(reply.snapshot_id != 0 && reply.generation == 1,
          "inventory has nonzero stable initial identities");
  require(reply.root_width == 1440 && reply.root_height == 600,
          "inventory reports the combined logical root extent");
  require(reply.descriptor_ids.size() == 2 && reply.modes.size() == 2 &&
              reply.states.size() == 2,
          "inventory reports every requested record class");
  require(std::ranges::find(reply.names, "LEFT") != reply.names.end() &&
              std::ranges::find(reply.names, "RIGHT") != reply.names.end(),
          "inventory preserves configured headless output names");
  require(reply.descriptor_ids[0] != 0 && reply.descriptor_ids[1] != 0 &&
              reply.descriptor_ids[0] != reply.descriptor_ids[1],
          "inventory assigns distinct stable output identifiers");
  require(std::ranges::any_of(reply.modes, [](const auto &mode) {
            return mode.physical_width == 800 && mode.physical_height == 600 &&
                   mode.refresh_millihertz == 60'000 && mode.current &&
                   mode.preferred;
          }) &&
              std::ranges::any_of(reply.modes, [](const auto &mode) {
                return mode.physical_width == 640 &&
                       mode.physical_height == 480 &&
                       mode.refresh_millihertz == 75'000 && mode.current &&
                       mode.preferred;
              }),
          "inventory preserves both configured headless modes");
  require(std::ranges::any_of(reply.states, [](const auto &state) {
            return state.logical_x == 0 && state.logical_y == 0 &&
                   state.logical_width == 800 && state.logical_height == 600;
          }) &&
              std::ranges::any_of(reply.states, [](const auto &state) {
                return state.logical_x == 800 && state.logical_y == 0 &&
                       state.logical_width == 640 &&
                       state.logical_height == 480;
              }),
          "inventory lays outputs left-to-right in logical coordinates");
  require(std::ranges::any_of(reply.states, [&](const auto &state) {
            return state.output_id == reply.primary_output_id;
          }),
          "inventory primary output identifies an enabled layout record");
}

template <typename Value>
void enqueue_contract(gwipc_connection *connection, const std::uint16_t type,
                      const std::uint32_t flags, const Value &value,
                      gwipc_status (*encode)(const Value *,
                                             gwipc_contract_payload **),
                      std::uint64_t *sequence = nullptr) {
  gwipc_contract_payload *raw = nullptr;
  require(encode(&value, &raw) == GWIPC_STATUS_OK,
          "encode scene contract record");
  ContractPayload payload(raw);
  std::size_t size = 0;
  const auto *bytes = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.flags = flags;
  message.payload = bytes;
  message.payload_size = size;
  const auto status = sequence
                          ? gwipc_connection_enqueue_with_sequence(
                                connection, &message, sequence)
                          : gwipc_connection_enqueue(connection, &message);
  require(status == GWIPC_STATUS_OK, "enqueue scene contract record");
}

template <typename Value>
void enqueue_control(gwipc_connection *connection, const std::uint16_t type,
                     const Value &value,
                     gwipc_status (*encode)(const Value *,
                                            gwipc_control_payload **)) {
  gwipc_control_payload *raw = nullptr;
  require(encode(&value, &raw) == GWIPC_STATUS_OK,
          "encode scene control record");
  ControlPayload payload(raw);
  std::size_t size = 0;
  const auto *bytes = gwipc_control_payload_data(payload.get(), &size);
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.payload = bytes;
  message.payload_size = size;
  require(gwipc_connection_enqueue(connection, &message) == GWIPC_STATUS_OK,
          "enqueue scene control record");
}

void submit_zero_surface_scene(gwipc_connection *connection,
                               const InventoryReply &inventory,
                               const std::uint64_t commit_id) {
  const auto item_count =
      static_cast<std::uint32_t>(inventory.states.size());
  gwipc_snapshot_begin begin{sizeof(begin),
                             commit_id,
                             GWIPC_SNAPSHOT_COMPLETE_SESSION,
                             0,
                             inventory.generation,
                             item_count,
                             {}};
  enqueue_control(connection, GWIPC_MESSAGE_SNAPSHOT_BEGIN, begin,
                  gwipc_control_encode_snapshot_begin);
  for (const auto &state : inventory.states)
    enqueue_contract(connection, GWIPC_MESSAGE_OUTPUT_UPSERT,
                     GWIPC_FLAG_SNAPSHOT_ITEM, state,
                     gwipc_contract_encode_output_upsert);
  gwipc_snapshot_end end{sizeof(end), commit_id, inventory.generation,
                         item_count, {}};
  enqueue_control(connection, GWIPC_MESSAGE_SNAPSHOT_END, end,
                  gwipc_control_encode_snapshot_end);
  gwipc_frame_commit commit{};
  commit.struct_size = sizeof(commit);
  commit.commit_id = commit_id;
  commit.output_id = 0;
  commit.producer_generation = inventory.generation;
  std::uint64_t commit_sequence = 0;
  enqueue_contract(connection, GWIPC_MESSAGE_FRAME_COMMIT,
                   GWIPC_FLAG_ACK_REQUIRED, commit,
                   gwipc_contract_encode_frame_commit, &commit_sequence);
  require(commit_sequence != 0, "scene commit receives a wire sequence");

  bool accepted = false;
  for (int attempt = 0; attempt < 300 && !accepted; ++attempt) {
    require(pump(connection), "drive zero-surface scene acceptance");
    for (;;) {
      gwipc_message *raw = nullptr;
      if (gwipc_connection_receive(connection, &raw) != GWIPC_STATUS_OK)
        break;
      Message message(raw);
      require(gwipc_message_type(message.get()) ==
                  GWIPC_MESSAGE_FRAME_ACKNOWLEDGED,
              "zero-surface scene receives only frame acknowledgement");
      auto decoded = decode_contract(message.get());
      const auto *acknowledged =
          gwipc_decoded_frame_acknowledged(decoded.get());
      require(acknowledged && acknowledged->commit_id == commit_id &&
                  acknowledged->output_id == 0 &&
                  acknowledged->presented_generation == inventory.generation &&
                  acknowledged->result == GWIPC_FRAME_ACCEPTED &&
                  gwipc_message_flags(message.get()) == GWIPC_FLAG_REPLY &&
                  gwipc_message_reply_to(message.get()) == commit_sequence,
              "zero-surface multi-output FrameCommit is accepted exactly");
      accepted = true;
    }
  }
  require(accepted, "zero-surface scene acceptance arrives before timeout");
}

}  // namespace

int main(int argc, char **argv) {
  require(argc == 2,
          "usage: gwcomp_output_inventory_process_test /path/to/gwcomp");
  char temporary[] = "/tmp/gwcomp-output-inventory-process-XXXXXX";
  require(::mkdtemp(temporary), "create temporary directory");
  const std::filesystem::path root = temporary;
  const auto socket = (root / "gwcomp.sock").string();
  const auto dumps = (root / "dumps").string();

  child_process = ::fork();
  require(child_process >= 0, "fork gwcomp");
  if (child_process == 0) {
    ::execl(argv[1], argv[1], "--ipc-socket", socket.c_str(), "--dump-dir",
            dumps.c_str(), "--headless-output", "LEFT:800x600@60000",
            "--headless-output", "RIGHT:640x480@75000", nullptr);
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
    require(::waitpid(child_process, &child_status, WNOHANG) == 0,
            "gwcomp remains alive while creating listener");
    (void)::usleep(10'000);
  }
  require(ready, "gwcomp output inventory listener becomes ready");

  require_historical_peer_rejected(socket);
  auto first_connection = connect_to(socket);
  const auto first_sequence = send_query(first_connection.get());
  const auto first =
      receive_inventory(first_connection.get(), first_sequence);
  validate_inventory(first);
  submit_zero_surface_scene(first_connection.get(), first, 101);
  first_connection.reset();

  auto second_connection = connect_to(socket);
  const auto second_sequence = send_query(second_connection.get());
  const auto second =
      receive_inventory(second_connection.get(), second_sequence);
  validate_inventory(second);
  require(second.snapshot_id > first.snapshot_id,
          "reconnect query receives a new monotonic snapshot identity");
  require(second.records == first.records && second.names == first.names &&
              second.primary_output_id == first.primary_output_id &&
              second.root_width == first.root_width &&
              second.root_height == first.root_height,
          "reconnect reproduces byte-identical inventory data records");
  submit_zero_surface_scene(second_connection.get(), second, 102);
  second_connection.reset();

  require(::kill(child_process, SIGTERM) == 0, "signal gwcomp");
  int child_status = 0;
  require(::waitpid(child_process, &child_status, 0) == child_process,
          "wait for gwcomp");
  child_process = -1;
  require(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0,
          "gwcomp stops cleanly after inventory reconnect");
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return 0;
}
