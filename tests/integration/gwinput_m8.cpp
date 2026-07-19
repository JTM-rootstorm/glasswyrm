#include <glasswyrm/ipc.h>

#include <poll.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
struct ConnectionDeleter {
  void operator()(gwipc_connection* value) const {
    gwipc_connection_destroy(value);
  }
};
struct MessageDeleter {
  void operator()(gwipc_message* value) const { gwipc_message_destroy(value); }
};
struct ContractDeleter {
  void operator()(gwipc_decoded_contract* value) const {
    gwipc_decoded_contract_destroy(value);
  }
};
struct PayloadDeleter {
  void operator()(gwipc_contract_payload* value) const {
    gwipc_contract_payload_destroy(value);
  }
};
using Connection = std::unique_ptr<gwipc_connection, ConnectionDeleter>;
using Message = std::unique_ptr<gwipc_message, MessageDeleter>;
using Contract = std::unique_ptr<gwipc_decoded_contract, ContractDeleter>;
using Payload = std::unique_ptr<gwipc_contract_payload, PayloadDeleter>;

struct Record {
  enum class Kind { motion, button, key, barrier } kind;
  std::uint64_t id;
  std::uint32_t time;
  std::int32_t x{};
  std::int32_t y{};
  std::uint8_t detail{};
  std::uint8_t pressed{};
};

void usage(FILE* output) {
  std::fprintf(output,
      "Usage: gwinput_m8 --socket PATH --scenario NAME --output PATH "
      "[--hold-until PATH] [--step-directory PATH]\n"
      "Scenarios: barrier, motion, crossing, buttons, button-motion, "
      "modifiers, keyboard, click-focus, invalid-transition, malformed, "
      "queue-limit, reconnect, m9-xeyes, m10-xeyes-repaint\n");
}
bool known_scenario(std::string_view name) {
  return name == "barrier" || name == "motion" || name == "crossing" ||
         name == "buttons" || name == "button-motion" ||
         name == "modifiers" || name == "keyboard" ||
         name == "click-focus" || name == "invalid-transition" ||
         name == "malformed" || name == "queue-limit" ||
         name == "reconnect" || name == "m9-xeyes" ||
         name == "m10-xeyes-repaint";
}

bool pump(gwipc_connection* connection, int timeout_ms) {
  pollfd descriptor{gwipc_connection_fd(connection),
                    gwipc_connection_wanted_poll_events(connection), 0};
  const int ready = ::poll(&descriptor, 1, timeout_ms);
  if (ready < 0 && errno == EINTR) return true;
  if (ready < 0) return false;
  if (ready > 0 && gwipc_connection_process_poll_events(
                       connection, descriptor.revents) ==
                       GWIPC_STATUS_SYSTEM_ERROR)
    return false;
  return gwipc_connection_get_state(connection) != GWIPC_CONNECTION_CLOSED;
}

bool hold_motion_step(gwipc_connection* connection,
                      const std::filesystem::path& directory,
                      const std::uint64_t input_id) {
  if (!std::filesystem::is_directory(directory)) return false;
  const auto stem = "step-" + std::to_string(input_id);
  const auto ready = directory / (stem + ".ready");
  const auto release = directory / (stem + ".release");
  std::ofstream ready_file(ready, std::ios::trunc);
  ready_file << input_id << '\n';
  ready_file.close();
  if (!ready_file) return false;
  for (int attempt = 0; attempt != 1200; ++attempt) {
    if (std::filesystem::exists(release)) return true;
    if (!pump(connection, 50)) return false;
  }
  return false;
}

Connection connect_to(const std::string& path) {
  gwipc_connection_options options{};
  options.struct_size = sizeof(options);
  options.path = path.c_str();
  options.local_role = GWIPC_ROLE_DIAGNOSTIC_TOOL;
  options.acceptable_server_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_PROTOCOL_SERVER);
  options.offered_capabilities = GWIPC_CAP_SYNTHETIC_INPUT;
  options.required_peer_capabilities = GWIPC_CAP_SYNTHETIC_INPUT;
  options.maximum_queued_bytes = GWIPC_HARD_MAXIMUM_QUEUED_BYTES;
  options.maximum_queued_messages = 8192;
  options.instance_label = "gwinput-m8";
  gwipc_connection* raw = nullptr;
  const auto status = gwipc_connection_connect(&options, &raw);
  if (status != GWIPC_STATUS_OK && status != GWIPC_STATUS_IN_PROGRESS) return {};
  Connection connection(raw);
  for (int attempt = 0; attempt < 200 &&
       gwipc_connection_get_state(connection.get()) !=
           GWIPC_CONNECTION_ESTABLISHED;
       ++attempt)
    if (!pump(connection.get(), 50)) return {};
  if (gwipc_connection_get_state(connection.get()) !=
      GWIPC_CONNECTION_ESTABLISHED)
    return {};
  return connection;
}

template <class Value, class Encoder>
bool enqueue(gwipc_connection* connection, std::uint16_t type,
             const Value& value, Encoder encoder) {
  gwipc_contract_payload* raw = nullptr;
  if (encoder(&value, &raw) != GWIPC_STATUS_OK) return false;
  const Payload payload(raw);
  std::size_t size = 0;
  const auto* bytes = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.flags = GWIPC_FLAG_ACK_REQUIRED;
  message.payload = bytes;
  message.payload_size = size;
  return gwipc_connection_enqueue(connection, &message) == GWIPC_STATUS_OK;
}

bool send(gwipc_connection* connection, const Record& record) {
  if (record.kind == Record::Kind::motion) {
    gwipc_synthetic_motion value{};
    value.struct_size = sizeof(value);
    value.input_id = record.id;
    value.time_ms = record.time;
    value.root_x = record.x;
    value.root_y = record.y;
    return enqueue(connection, GWIPC_MESSAGE_SYNTHETIC_MOTION, value,
                   gwipc_contract_encode_synthetic_motion);
  }
  if (record.kind == Record::Kind::button) {
    gwipc_synthetic_button value{};
    value.struct_size = sizeof(value);
    value.input_id = record.id;
    value.time_ms = record.time;
    value.button = record.detail;
    value.pressed = record.pressed;
    return enqueue(connection, GWIPC_MESSAGE_SYNTHETIC_BUTTON, value,
                   gwipc_contract_encode_synthetic_button);
  }
  if (record.kind == Record::Kind::key) {
    gwipc_synthetic_key value{};
    value.struct_size = sizeof(value);
    value.input_id = record.id;
    value.time_ms = record.time;
    value.keycode = record.detail;
    value.pressed = record.pressed;
    return enqueue(connection, GWIPC_MESSAGE_SYNTHETIC_KEY, value,
                   gwipc_contract_encode_synthetic_key);
  }
  gwipc_synthetic_barrier value{};
  value.struct_size = sizeof(value);
  value.input_id = record.id;
  return enqueue(connection, GWIPC_MESSAGE_SYNTHETIC_BARRIER, value,
                 gwipc_contract_encode_synthetic_barrier);
}

bool receive_ack(gwipc_connection* connection,
                 gwipc_synthetic_input_acknowledged& result) {
  for (int attempt = 0; attempt < 400; ++attempt) {
    if (!pump(connection, 50)) return false;
    gwipc_message* raw_message = nullptr;
    if (gwipc_connection_receive(connection, &raw_message) != GWIPC_STATUS_OK)
      continue;
    const Message message(raw_message);
    if (gwipc_message_type(message.get()) !=
            GWIPC_MESSAGE_SYNTHETIC_INPUT_ACKNOWLEDGED ||
        gwipc_message_flags(message.get()) != GWIPC_FLAG_REPLY)
      return false;
    gwipc_decoded_contract* raw_contract = nullptr;
    if (gwipc_contract_decode_message(message.get(), &raw_contract) !=
        GWIPC_STATUS_OK)
      return false;
    const Contract contract(raw_contract);
    const auto* ack = gwipc_decoded_synthetic_input_acknowledged(contract.get());
    if (ack == nullptr) return false;
    result = *ack;
    return true;
  }
  return false;
}

std::vector<Record> scenario(std::string_view name) {
  using K = Record::Kind;
  std::vector<Record> result;
  auto add = [&](K kind, std::uint32_t time, std::int32_t x = 0,
                 std::int32_t y = 0, std::uint8_t detail = 0,
                 std::uint8_t pressed = 0) {
    result.push_back({kind, result.size() + 1, time, x, y, detail, pressed});
  };
  if (name == "barrier") {
  } else if (name == "motion" || name == "crossing") {
    add(K::motion, 2, 100, 100);
    add(K::motion, 3, 700, 500);
    add(K::barrier, 0);
  } else if (name == "buttons" || name == "click-focus") {
    add(K::motion, 2, 80, 80);
    add(K::button, 3, 0, 0, 1, 1);
    add(K::button, 4, 0, 0, 1, 0);
    add(K::barrier, 0);
  } else if (name == "button-motion") {
    add(K::motion, 2, 80, 80);
    add(K::button, 3, 0, 0, 1, 1);
    add(K::motion, 4, 120, 110);
    add(K::button, 5, 0, 0, 1, 0);
  } else if (name == "modifiers") {
    add(K::key, 2, 0, 0, 50, 1);
    add(K::key, 3, 0, 0, 38, 1);
    add(K::key, 4, 0, 0, 38, 0);
    add(K::key, 5, 0, 0, 50, 0);
  } else if (name == "keyboard") {
    add(K::key, 2, 0, 0, 38, 1);
    add(K::key, 3, 0, 0, 38, 0);
    add(K::barrier, 0);
  } else if (name == "invalid-transition") {
    add(K::button, 2, 0, 0, 1, 0);
    add(K::key, 2, 0, 0, 38, 0);
  } else if (name == "queue-limit") {
    for (std::uint32_t index = 0; index != 4097; ++index)
      add(K::motion, index + 2, index % 1024, index % 768);
  } else if (name == "malformed") {
    add(K::key, 2, 0, 0, 7, 1);
  } else if (name == "reconnect") {
    add(K::motion, 2, 15, 20);
    add(K::barrier, 0);
  } else if (name == "m9-xeyes") {
    add(K::motion, 2, 0, 60);
    add(K::barrier, 0);
    add(K::motion, 3, 35, 55);
    add(K::barrier, 0);
    add(K::motion, 4, 110, 55);
    add(K::barrier, 0);
    add(K::motion, 5, 700, 500);
    add(K::barrier, 0);
  } else if (name == "m10-xeyes-repaint") {
    add(K::motion, 2, 35, 55);
    add(K::barrier, 0);
  }
  return result;
}

const char* result_name(gwipc_synthetic_input_result value) {
  switch (value) {
    case GWIPC_SYNTHETIC_INPUT_ACCEPTED: return "accepted";
    case GWIPC_SYNTHETIC_INPUT_CLAMPED: return "clamped";
    case GWIPC_SYNTHETIC_INPUT_INVALID_TRANSITION: return "invalid-transition";
    case GWIPC_SYNTHETIC_INPUT_FOCUS_UNCHANGED: return "focus-unchanged";
    case GWIPC_SYNTHETIC_INPUT_FOCUS_REJECTED: return "focus-rejected";
    case GWIPC_SYNTHETIC_INPUT_LIMIT_EXCEEDED: return "limit-exceeded";
  }
  return "unknown";
}
}  // namespace

int main(int argc, char** argv) {
  std::string socket, name, output, hold_until, step_directory;
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg(argv[index]);
    if (arg == "--help") { usage(stdout); return 0; }
    if (index + 1 >= argc) { usage(stderr); return 2; }
    if (arg == "--socket") socket = argv[++index];
    else if (arg == "--scenario") name = argv[++index];
    else if (arg == "--output") output = argv[++index];
    else if (arg == "--hold-until") hold_until = argv[++index];
    else if (arg == "--step-directory") step_directory = argv[++index];
    else { usage(stderr); return 2; }
  }
  if (socket.empty() || output.empty() || !known_scenario(name)) {
    usage(stderr);
    return 2;
  }
  auto connection = connect_to(socket);
  if (!connection) { std::fprintf(stderr, "gwinput_m8: connection failed\n"); return 1; }
  std::vector<gwipc_synthetic_input_acknowledged> acknowledgements;
  const Record initial{Record::Kind::barrier, 1, 0};
  if (!send(connection.get(), initial)) return 1;
  gwipc_synthetic_input_acknowledged initial_ack{};
  if (!receive_ack(connection.get(), initial_ack) || initial_ack.input_id != 1)
    return 1;
  acknowledgements.push_back(initial_ack);
  auto records = scenario(name);
  for (auto& record : records) {
    ++record.id;
    if (record.time != 0) record.time += initial_ack.time_ms - 1;
    if (!send(connection.get(), record)) return 1;
    gwipc_synthetic_input_acknowledged ack{};
    if (!receive_ack(connection.get(), ack) || ack.input_id != record.id) return 1;
    acknowledgements.push_back(ack);
    if (!step_directory.empty() && record.kind == Record::Kind::motion &&
        !hold_motion_step(connection.get(), step_directory, record.id))
      return 1;
    if ((name == "m9-xeyes" || name == "m10-xeyes-repaint") &&
        record.kind == Record::Kind::barrier)
      std::this_thread::sleep_for(std::chrono::milliseconds(350));
  }
  std::ofstream stream(output);
  if (!stream) return 1;
  stream << "{\"scenario\":\"" << name << "\",\"acknowledgements\":[";
  for (std::size_t index = 0; index < acknowledgements.size(); ++index) {
    const auto& ack = acknowledgements[index];
    if (index != 0) stream << ',';
    stream << "{\"input_id\":" << ack.input_id << ",\"time_ms\":"
           << ack.time_ms << ",\"result\":\"" << result_name(ack.result)
           << "\",\"root_x\":" << ack.root_x << ",\"root_y\":"
           << ack.root_y << ",\"pointer_window\":" << ack.pointer_window
           << ",\"focus_window\":" << ack.focus_window << ",\"state\":"
           << ack.state << ",\"delivered_event_count\":"
           << ack.delivered_event_count << '}';
  }
  stream << "]}\n";
  stream.close();
  if (!stream) return 1;
  if (!hold_until.empty()) {
    bool released = false;
    for (int attempt = 0; attempt != 400; ++attempt) {
      if (std::ifstream(hold_until)) {
        released = true;
        break;
      }
      if (!pump(connection.get(), 50)) return 1;
    }
    if (!released) return 1;
  }
  return 0;
}
