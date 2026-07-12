#include <glasswyrm/ipc.h>

#include <poll.h>

#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr std::uint64_t kCapabilities =
    GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_WINDOW_POLICY;

struct ConnectionDeleter { void operator()(gwipc_connection* p) const { gwipc_connection_destroy(p); } };
struct MessageDeleter { void operator()(gwipc_message* p) const { gwipc_message_destroy(p); } };
struct ContractDeleter { void operator()(gwipc_decoded_contract* p) const { gwipc_decoded_contract_destroy(p); } };
struct ContractPayloadDeleter { void operator()(gwipc_contract_payload* p) const { gwipc_contract_payload_destroy(p); } };
struct ControlDeleter { void operator()(gwipc_decoded_control* p) const { gwipc_decoded_control_destroy(p); } };
struct ControlPayloadDeleter { void operator()(gwipc_control_payload* p) const { gwipc_control_payload_destroy(p); } };
using Connection = std::unique_ptr<gwipc_connection, ConnectionDeleter>;
using Message = std::unique_ptr<gwipc_message, MessageDeleter>;

struct Scenario {
  gwipc_policy_context_upsert context{};
  std::vector<gwipc_policy_window_upsert> windows;
};
struct Reply {
  gwipc_policy_acknowledged ack{};
  std::vector<gwipc_policy_window_state> windows;
};

void usage(FILE* output) {
  std::fprintf(output,
      "Usage: gwm_m5_producer --socket PATH --scenario NAME --output PATH\n"
      "Scenarios: basic, snapshot-order, transient, override-redirect, focus, "
      "stacking, fullscreen, maximize-minimize\n");
}

bool pump(gwipc_connection* connection, int timeout_ms) {
  pollfd descriptor{gwipc_connection_fd(connection),
                    gwipc_connection_wanted_poll_events(connection), 0};
  const int ready = ::poll(&descriptor, 1, timeout_ms);
  if (ready < 0 && errno == EINTR) return true;
  if (ready < 0) return false;
  if (ready > 0 && gwipc_connection_process_poll_events(connection,
          descriptor.revents) == GWIPC_STATUS_SYSTEM_ERROR) return false;
  return gwipc_connection_get_state(connection) != GWIPC_CONNECTION_CLOSED;
}

bool enqueue_bytes(gwipc_connection* connection, std::uint16_t type,
                   std::uint32_t flags, const std::uint8_t* bytes,
                   std::size_t size) {
  gwipc_outgoing_message outgoing{};
  outgoing.struct_size = sizeof(outgoing); outgoing.type = type;
  outgoing.flags = flags; outgoing.payload = bytes; outgoing.payload_size = size;
  return gwipc_connection_enqueue(connection, &outgoing) == GWIPC_STATUS_OK;
}

template <class Value, class Encoder>
bool send_contract(gwipc_connection* connection, std::uint16_t type,
                   std::uint32_t flags, const Value& value, Encoder encoder) {
  gwipc_contract_payload* raw = nullptr;
  if (encoder(&value, &raw) != GWIPC_STATUS_OK) return false;
  const std::unique_ptr<gwipc_contract_payload, ContractPayloadDeleter> payload(raw);
  std::size_t size = 0;
  const auto* bytes = gwipc_contract_payload_data(payload.get(), &size);
  return enqueue_bytes(connection, type, flags, bytes, size);
}

template <class Value, class Encoder>
bool send_control(gwipc_connection* connection, std::uint16_t type,
                  const Value& value, Encoder encoder) {
  gwipc_control_payload* raw = nullptr;
  if (encoder(&value, &raw) != GWIPC_STATUS_OK) return false;
  const std::unique_ptr<gwipc_control_payload, ControlPayloadDeleter> payload(raw);
  std::size_t size = 0;
  const auto* bytes = gwipc_control_payload_data(payload.get(), &size);
  return enqueue_bytes(connection, type, 0, bytes, size);
}

gwipc_policy_window_upsert window(std::uint32_t id, std::uint64_t serial) {
  gwipc_policy_window_upsert value{};
  value.struct_size = sizeof(value); value.window_id = id;
  value.parent_window_id = 1; value.requested_width = 240;
  value.requested_height = 160; value.window_type = GWIPC_POLICY_WINDOW_NORMAL;
  value.map_intent = GWIPC_POLICY_WANTS_MAP;
  value.decoration_preference = GWIPC_TRI_STATE_UNKNOWN;
  value.creation_serial = serial; value.map_serial = serial;
  return value;
}

Scenario make_scenario(std::string_view name) {
  Scenario result;
  result.context.struct_size = sizeof(result.context);
  result.context.root_window_id = 1; result.context.workspace_id = 1;
  result.context.output_id = 1; result.context.work_width = 1024;
  result.context.work_height = 768;
  if (name == "basic" || name == "snapshot-order") {
    result.windows = {window(1001, 1), window(1002, 2), window(1003, 3)};
    if (name == "snapshot-order") std::reverse(result.windows.begin(), result.windows.end());
  } else if (name == "transient") {
    auto parent = window(1101, 1);
    auto child = window(1102, 2); child.transient_for = 1101;
    child.window_type = GWIPC_POLICY_WINDOW_DIALOG;
    child.requested_width = 320; child.requested_height = 200;
    result.windows = {child, parent};
  } else if (name == "override-redirect") {
    auto managed = window(1201, 1);
    auto bypass = window(1202, 2); bypass.override_redirect = 1;
    bypass.requested_x = -40; bypass.requested_y = 700;
    bypass.requested_width = 900; bypass.requested_height = 100;
    bypass.focus_serial = 99;
    result.windows = {bypass, managed};
  } else if (name == "focus") {
    auto first = window(1301, 1); first.focus_serial = 50;
    result.windows = {first, window(1302, 2), window(1303, 3)};
  } else if (name == "stacking") {
    auto first = window(1401, 1); first.map_serial = 30;
    auto second = window(1402, 2); second.map_serial = 10;
    auto transient = window(1403, 3); transient.map_serial = 20;
    transient.transient_for = 1402; transient.window_type = GWIPC_POLICY_WINDOW_DIALOG;
    auto bypass = window(1404, 4); bypass.map_serial = 5; bypass.override_redirect = 1;
    result.windows = {first, second, transient, bypass};
  } else if (name == "fullscreen") {
    auto full = window(1501, 1); full.fullscreen_requested = 1;
    result.windows = {full};
  } else if (name == "maximize-minimize") {
    auto maximum = window(1601, 1); maximum.maximized_requested = 1;
    auto minimum = window(1602, 2); minimum.minimized_requested = 1;
    minimum.fullscreen_requested = 1; minimum.focus_serial = 100;
    result.windows = {maximum, minimum};
  }
  return result;
}

bool send_snapshot(gwipc_connection* connection, const Scenario& scenario) {
  const auto count = static_cast<std::uint32_t>(scenario.windows.size() + 1U);
  gwipc_snapshot_begin begin{};
  begin.struct_size = sizeof(begin); begin.snapshot_id = 1;
  begin.domain = GWIPC_SNAPSHOT_WINDOW_POLICY; begin.generation = 1;
  begin.expected_item_count = count;
  if (!send_control(connection, GWIPC_MESSAGE_SNAPSHOT_BEGIN, begin,
                    gwipc_control_encode_snapshot_begin) ||
      !send_contract(connection, GWIPC_MESSAGE_POLICY_CONTEXT_UPSERT,
                     GWIPC_FLAG_SNAPSHOT_ITEM, scenario.context,
                     gwipc_contract_encode_policy_context_upsert)) return false;
  for (const auto& item : scenario.windows)
    if (!send_contract(connection, GWIPC_MESSAGE_POLICY_WINDOW_UPSERT,
                       GWIPC_FLAG_SNAPSHOT_ITEM, item,
                       gwipc_contract_encode_policy_window_upsert)) return false;
  gwipc_snapshot_end end{};
  end.struct_size = sizeof(end); end.snapshot_id = begin.snapshot_id;
  end.generation = begin.generation; end.actual_item_count = count;
  if (!send_control(connection, GWIPC_MESSAGE_SNAPSHOT_END, end,
                    gwipc_control_encode_snapshot_end)) return false;
  gwipc_policy_commit commit{};
  commit.struct_size = sizeof(commit); commit.commit_id = 100;
  commit.producer_generation = 1;
  return send_contract(connection, GWIPC_MESSAGE_POLICY_COMMIT,
                       GWIPC_FLAG_ACK_REQUIRED, commit,
                       gwipc_contract_encode_policy_commit);
}

bool receive_reply(gwipc_connection* connection, Reply& reply) {
  bool active = false;
  std::uint64_t snapshot_id = 0, generation = 0;
  std::uint32_t expected = 0;
  for (int attempt = 0; attempt < 400; ++attempt) {
    if (!pump(connection, 50)) return false;
    gwipc_message* raw = nullptr;
    if (gwipc_connection_receive(connection, &raw) != GWIPC_STATUS_OK) continue;
    const Message message(raw);
    const auto type = gwipc_message_type(message.get());
    if (type == GWIPC_MESSAGE_SNAPSHOT_BEGIN || type == GWIPC_MESSAGE_SNAPSHOT_END) {
      gwipc_decoded_control* decoded_raw = nullptr;
      if (gwipc_control_decode_message(message.get(), &decoded_raw) != GWIPC_STATUS_OK)
        return false;
      const std::unique_ptr<gwipc_decoded_control, ControlDeleter> decoded(decoded_raw);
      if (type == GWIPC_MESSAGE_SNAPSHOT_BEGIN) {
        const auto* begin = gwipc_decoded_snapshot_begin(decoded.get());
        if (!begin || active || begin->domain != GWIPC_SNAPSHOT_WINDOW_POLICY ||
            begin->snapshot_id == 0) return false;
        active = true; snapshot_id = begin->snapshot_id;
        generation = begin->generation; expected = begin->expected_item_count;
        reply.windows.clear();
      } else {
        const auto* end = gwipc_decoded_snapshot_end(decoded.get());
        if (!end || !active || end->snapshot_id != snapshot_id ||
            end->generation != generation || end->actual_item_count != expected ||
            reply.windows.size() != expected) return false;
        active = false;
      }
      continue;
    }
    gwipc_decoded_contract* decoded_raw = nullptr;
    if (gwipc_contract_decode_message(message.get(), &decoded_raw) != GWIPC_STATUS_OK)
      return false;
    const std::unique_ptr<gwipc_decoded_contract, ContractDeleter> decoded(decoded_raw);
    if (type == GWIPC_MESSAGE_POLICY_WINDOW_STATE) {
      const auto* state = gwipc_decoded_policy_window_state(decoded.get());
      if (!active || !state ||
          gwipc_message_flags(message.get()) != GWIPC_FLAG_SNAPSHOT_ITEM) return false;
      reply.windows.push_back(*state);
    } else if (type == GWIPC_MESSAGE_POLICY_ACKNOWLEDGED) {
      const auto* ack = gwipc_decoded_policy_acknowledged(decoded.get());
      if (active || !ack || gwipc_message_flags(message.get()) != GWIPC_FLAG_REPLY ||
          ack->commit_id != 100 || ack->producer_generation != 1 ||
          ack->applied_generation != generation ||
          ack->result != GWIPC_POLICY_ACCEPTED ||
          ack->window_count != reply.windows.size()) return false;
      reply.ack = *ack;
      return true;
    } else return false;
  }
  return false;
}

const char* applied(gwipc_policy_applied_state value) {
  switch (value) { case GWIPC_POLICY_APPLIED_MAXIMIZED: return "Maximized";
    case GWIPC_POLICY_APPLIED_FULLSCREEN: return "Fullscreen";
    case GWIPC_POLICY_APPLIED_MINIMIZED: return "Minimized";
    default: return "Normal"; }
}
const char* tri(gwipc_tri_state value) {
  switch (value) { case GWIPC_TRI_STATE_FALSE: return "False";
    case GWIPC_TRI_STATE_TRUE: return "True"; default: return "Unknown"; }
}
const char* boolean(std::uint8_t value) { return value ? "true" : "false"; }

bool write_json(const char* path, std::string_view scenario, const Reply& reply) {
  const std::filesystem::path destination(path);
  std::error_code error;
  if (!destination.parent_path().empty())
    std::filesystem::create_directories(destination.parent_path(), error);
  if (error) return false;
  std::ofstream output(destination, std::ios::binary | std::ios::trunc);
  if (!output) return false;
  char hash[17]{};
  std::snprintf(hash, sizeof(hash), "%016" PRIx64, reply.ack.policy_hash);
  output << "{\n  \"scenario\": \"" << scenario
         << "\",\n  \"commit_id\": " << reply.ack.commit_id
         << ",\n  \"producer_generation\": " << reply.ack.producer_generation
         << ",\n  \"applied_generation\": " << reply.ack.applied_generation
         << ",\n  \"result\": \"Accepted\",\n  \"policy_hash\": \"" << hash
         << "\",\n  \"window_count\": " << reply.ack.window_count
         << ",\n  \"windows\": [\n";
  for (std::size_t index = 0; index < reply.windows.size(); ++index) {
    const auto& w = reply.windows[index];
    output << "    {\n      \"window_id\": " << w.window_id
           << ",\n      \"workspace_id\": " << w.workspace_id
           << ",\n      \"output_id\": " << w.output_id
           << ",\n      \"x\": " << w.final_x << ",\n      \"y\": " << w.final_y
           << ",\n      \"width\": " << w.final_width
           << ",\n      \"height\": " << w.final_height
           << ",\n      \"stacking\": " << w.stacking
           << ",\n      \"visible\": " << boolean(w.visible)
           << ",\n      \"focused\": " << boolean(w.focused)
           << ",\n      \"managed\": " << boolean(w.managed)
           << ",\n      \"decoration_eligible\": " << boolean(w.decoration_eligible)
           << ",\n      \"applied_state\": \"" << applied(w.applied_state)
           << "\",\n      \"fullscreen_eligible\": \"" << tri(w.fullscreen_eligible)
           << "\",\n      \"direct_scanout_eligible\": \"" << tri(w.direct_scanout_eligible)
           << "\"\n    }" << (index + 1 == reply.windows.size() ? "\n" : ",\n");
  }
  output << "  ]\n}\n";
  return output.good();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--help") { usage(stdout); return 0; }
  const char* socket = nullptr; const char* output = nullptr; std::string_view scenario;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if ((argument == "--socket" || argument == "--scenario" || argument == "--output") &&
        index + 1 < argc) {
      const char* value = argv[++index];
      if (argument == "--socket") socket = value;
      else if (argument == "--scenario") scenario = value;
      else output = value;
    } else { usage(stderr); return 2; }
  }
  constexpr std::string_view names[] = {"basic", "snapshot-order", "transient",
      "override-redirect", "focus", "stacking", "fullscreen", "maximize-minimize"};
  if (!socket || !output || scenario.empty() ||
      std::find(std::begin(names), std::end(names), scenario) == std::end(names)) {
    usage(stderr); return 2;
  }
  gwipc_connection_options options{};
  options.struct_size = sizeof(options); options.path = socket;
  options.local_role = GWIPC_ROLE_PROTOCOL_SERVER;
  options.acceptable_server_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_WINDOW_MANAGER);
  options.offered_capabilities = kCapabilities;
  options.required_peer_capabilities = kCapabilities;
  options.instance_label = "gwm-m5-producer";
  gwipc_connection* raw = nullptr;
  const auto status = gwipc_connection_connect(&options, &raw);
  if (status != GWIPC_STATUS_OK && status != GWIPC_STATUS_IN_PROGRESS) return 1;
  const Connection connection(raw);
  for (int attempt = 0; attempt < 200 &&
       gwipc_connection_get_state(connection.get()) != GWIPC_CONNECTION_ESTABLISHED;
       ++attempt) if (!pump(connection.get(), 50)) return 1;
  Reply reply;
  if (gwipc_connection_get_state(connection.get()) != GWIPC_CONNECTION_ESTABLISHED ||
      !send_snapshot(connection.get(), make_scenario(scenario)) ||
      !receive_reply(connection.get(), reply) || !write_json(output, scenario, reply)) return 1;
  return 0;
}
