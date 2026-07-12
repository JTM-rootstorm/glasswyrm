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
      "stacking, fullscreen, maximize-minimize, incremental-update, "
      "invalid-context, invalid-window, unknown-reference, transient-cycle, "
      "snapshot-abort, snapshot-reconnect\n");
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

bool send_commit(gwipc_connection* connection, std::uint64_t commit_id,
                 std::uint64_t generation) {
  gwipc_policy_commit commit{};
  commit.struct_size = sizeof(commit); commit.commit_id = commit_id;
  commit.producer_generation = generation;
  return send_contract(connection, GWIPC_MESSAGE_POLICY_COMMIT,
                       GWIPC_FLAG_ACK_REQUIRED, commit,
                       gwipc_contract_encode_policy_commit);
}

bool send_snapshot(gwipc_connection* connection, const Scenario& scenario,
                   std::uint64_t snapshot_id = 1,
                   std::uint64_t commit_id = 100,
                   std::uint64_t generation = 1) {
  const auto count = static_cast<std::uint32_t>(scenario.windows.size() + 1U);
  gwipc_snapshot_begin begin{};
  begin.struct_size = sizeof(begin); begin.snapshot_id = snapshot_id;
  begin.domain = GWIPC_SNAPSHOT_WINDOW_POLICY; begin.generation = generation;
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
  return send_commit(connection, commit_id, generation);
}

bool receive_reply(gwipc_connection* connection, Reply& reply,
                   std::uint64_t commit_id = 100,
                   std::uint64_t producer_generation = 1,
                   gwipc_policy_result expected_result = GWIPC_POLICY_ACCEPTED,
                   std::uint64_t previous_hash = 0) {
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
      const bool accepted = expected_result == GWIPC_POLICY_ACCEPTED;
      if (active || !ack || gwipc_message_flags(message.get()) != GWIPC_FLAG_REPLY ||
          ack->commit_id != commit_id ||
          ack->producer_generation != producer_generation ||
          ack->result != expected_result ||
          (accepted && (ack->applied_generation != generation ||
                        ack->window_count != reply.windows.size())) ||
          (!accepted && (!reply.windows.empty() || ack->policy_hash != previous_hash ||
                         ack->applied_generation != 1 || ack->window_count != 3)))
        return false;
      reply.ack = *ack;
      return true;
    } else return false;
  }
  return false;
}

Connection connect_to(const char* socket) {
  gwipc_connection_options options{};
  options.struct_size = sizeof(options); options.path = socket;
  options.local_role = GWIPC_ROLE_PROTOCOL_SERVER;
  options.acceptable_server_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_WINDOW_MANAGER);
  options.offered_capabilities = kCapabilities;
  options.required_peer_capabilities = kCapabilities;
  options.instance_label = "gwm-m5-producer";
  gwipc_connection* raw = nullptr;
  const auto status = gwipc_connection_connect(&options, &raw);
  if (status != GWIPC_STATUS_OK && status != GWIPC_STATUS_IN_PROGRESS) return {};
  Connection connection(raw);
  for (int attempt = 0; attempt < 200 &&
       gwipc_connection_get_state(connection.get()) != GWIPC_CONNECTION_ESTABLISHED;
       ++attempt) if (!pump(connection.get(), 50)) return {};
  if (gwipc_connection_get_state(connection.get()) != GWIPC_CONNECTION_ESTABLISHED)
    return {};
  return connection;
}

bool send_window(gwipc_connection* connection,
                 const gwipc_policy_window_upsert& value) {
  return send_contract(connection, GWIPC_MESSAGE_POLICY_WINDOW_UPSERT, 0, value,
                       gwipc_contract_encode_policy_window_upsert);
}
bool send_context(gwipc_connection* connection,
                  const gwipc_policy_context_upsert& value) {
  return send_contract(connection, GWIPC_MESSAGE_POLICY_CONTEXT_UPSERT, 0, value,
                       gwipc_contract_encode_policy_context_upsert);
}
bool send_remove(gwipc_connection* connection, std::uint32_t id) {
  gwipc_policy_window_remove value{};
  value.struct_size = sizeof(value); value.window_id = id;
  return send_contract(connection, GWIPC_MESSAGE_POLICY_WINDOW_REMOVE, 0, value,
                       gwipc_contract_encode_policy_window_remove);
}

bool same_windows(const Reply& left, const Reply& right) {
  if (left.windows.size() != right.windows.size()) return false;
  for (std::size_t index = 0; index < left.windows.size(); ++index) {
    const auto& a = left.windows[index]; const auto& b = right.windows[index];
    if (a.window_id != b.window_id || a.final_x != b.final_x ||
        a.final_y != b.final_y || a.final_width != b.final_width ||
        a.final_height != b.final_height || a.stacking != b.stacking ||
        a.visible != b.visible || a.focused != b.focused ||
        a.managed != b.managed ||
        a.decoration_eligible != b.decoration_eligible ||
        a.applied_state != b.applied_state) return false;
  }
  return true;
}

bool bootstrap(gwipc_connection* connection, Reply& reply) {
  return send_snapshot(connection, make_scenario("basic")) &&
         receive_reply(connection, reply);
}

bool run_extended(gwipc_connection* connection, std::string_view name,
                  Reply& final_reply) {
  Reply initial;
  if (!bootstrap(connection, initial)) return false;
  if (name == "incremental-update") {
    auto changed = window(1002, 2); changed.focus_serial = 20;
    auto added = window(1004, 4);
    return send_window(connection, changed) && send_remove(connection, 1001) &&
           send_window(connection, added) && send_commit(connection, 101, 2) &&
           receive_reply(connection, final_reply, 101, 2) &&
           final_reply.windows.size() == 3 &&
           std::none_of(final_reply.windows.begin(), final_reply.windows.end(),
                        [](const auto& state) { return state.window_id == 1001; }) &&
           std::any_of(final_reply.windows.begin(), final_reply.windows.end(),
                       [](const auto& state) {
                         return state.window_id == 1002 && state.focused;
                       }) &&
           std::any_of(final_reply.windows.begin(), final_reply.windows.end(),
                       [](const auto& state) { return state.window_id == 1004; });
  }

  gwipc_policy_result rejection = GWIPC_POLICY_REJECTED_INVALID_WINDOW;
  if (name == "invalid-context") {
    auto bad = make_scenario("basic").context; bad.work_width = 20'000;
    if (!send_context(connection, bad)) return false;
    rejection = GWIPC_POLICY_REJECTED_INVALID_CONTEXT;
  } else if (name == "invalid-window") {
    auto bad = window(1002, 1);
    if (!send_window(connection, bad)) return false;
  } else if (name == "unknown-reference") {
    auto bad = window(1002, 2); bad.transient_for = 9999;
    if (!send_window(connection, bad)) return false;
    rejection = GWIPC_POLICY_REJECTED_UNKNOWN_REFERENCE;
  } else if (name == "transient-cycle") {
    auto first = window(1001, 1); first.transient_for = 1002;
    auto second = window(1002, 2); second.transient_for = 1001;
    if (!send_window(connection, first) || !send_window(connection, second)) return false;
  } else if (name == "snapshot-abort") {
    gwipc_snapshot_begin begin{};
    begin.struct_size = sizeof(begin); begin.snapshot_id = 2;
    begin.domain = GWIPC_SNAPSHOT_WINDOW_POLICY; begin.generation = 2;
    begin.expected_item_count = 2;
    auto replacement = make_scenario("basic");
    gwipc_snapshot_abort abort{};
    abort.struct_size = sizeof(abort); abort.snapshot_id = 2; abort.reason = 1;
    if (!send_control(connection, GWIPC_MESSAGE_SNAPSHOT_BEGIN, begin,
                      gwipc_control_encode_snapshot_begin) ||
        !send_contract(connection, GWIPC_MESSAGE_POLICY_CONTEXT_UPSERT,
                       GWIPC_FLAG_SNAPSHOT_ITEM, replacement.context,
                       gwipc_contract_encode_policy_context_upsert) ||
        !send_control(connection, GWIPC_MESSAGE_SNAPSHOT_ABORT, abort,
                      gwipc_control_encode_snapshot_abort) ||
        !send_commit(connection, 101, 2) ||
        !receive_reply(connection, final_reply, 101, 2)) return false;
    return same_windows(initial, final_reply);
  } else return false;

  Reply rejected;
  if (!send_commit(connection, 101, 2) ||
      !receive_reply(connection, rejected, 101, 2, rejection,
                     initial.ack.policy_hash)) return false;
  if (name == "invalid-context") {
    if (!send_context(connection, make_scenario("basic").context)) return false;
  } else {
    auto corrected = window(1002, 2);
    if (!send_window(connection, corrected)) return false;
    if (name == "transient-cycle") {
      auto first = window(1001, 1);
      if (!send_window(connection, first)) return false;
    }
  }
  return send_commit(connection, 102, 2) &&
         receive_reply(connection, final_reply, 102, 2) &&
         same_windows(initial, final_reply);
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
      "override-redirect", "focus", "stacking", "fullscreen", "maximize-minimize",
      "incremental-update", "invalid-context", "invalid-window",
      "unknown-reference", "transient-cycle", "snapshot-abort",
      "snapshot-reconnect"};
  if (!socket || !output || scenario.empty() ||
      std::find(std::begin(names), std::end(names), scenario) == std::end(names)) {
    usage(stderr); return 2;
  }
  auto connection = connect_to(socket);
  if (!connection) return 1;
  Reply reply;
  const bool initial = scenario == "basic" || scenario == "snapshot-order" ||
      scenario == "transient" || scenario == "override-redirect" ||
      scenario == "focus" || scenario == "stacking" || scenario == "fullscreen" ||
      scenario == "maximize-minimize";
  bool ok = false;
  if (initial) {
    ok = send_snapshot(connection.get(), make_scenario(scenario)) &&
         receive_reply(connection.get(), reply);
  } else if (scenario == "snapshot-reconnect") {
    Reply first;
    ok = bootstrap(connection.get(), first);
    connection.reset();
    if (ok) {
      connection = connect_to(socket);
      ok = connection && bootstrap(connection.get(), reply) &&
           first.ack.policy_hash == reply.ack.policy_hash && same_windows(first, reply);
    }
  } else {
    ok = run_extended(connection.get(), scenario, reply);
  }
  if (!ok || !write_json(output, scenario, reply)) return 1;
  return 0;
}
