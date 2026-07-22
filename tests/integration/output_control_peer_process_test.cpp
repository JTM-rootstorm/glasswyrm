#include "glasswyrmd/output_control_peer.hpp"

#include "output/model/layout.hpp"
#include "tests/helpers/test_support.hpp"

#include <glasswyrm/ipc.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <poll.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

using glasswyrm::output::OutputDescriptor;
using glasswyrm::output::OutputId;
using glasswyrm::output::OutputLayout;
using glasswyrm::output::OutputModeId;
using glasswyrm::output::OutputState;
using glasswyrm::server::OutputControlPeer;
using glasswyrm::server::VrrStateCache;
using gw::test::require;

struct ConnectionDelete {
  void operator()(gwipc_connection *value) const noexcept {
    gwipc_connection_destroy(value);
  }
};

using Connection = std::unique_ptr<gwipc_connection, ConnectionDelete>;

OutputLayout inventory() {
  OutputLayout layout;
  for (std::uint64_t index = 0; index < 2; ++index) {
    const OutputId id{11 + index};
    const OutputModeId mode_id{21 + index};
    OutputDescriptor descriptor;
    descriptor.id = id;
    descriptor.name = index == 0 ? "LEFT" : "RIGHT";
    descriptor.connected = true;
    descriptor.mode_configurable = true;
    descriptor.scale_configurable = true;
    descriptor.transform_configurable = true;
    descriptor.primary_eligible = true;
    descriptor.arbitrary_headless_mode = true;
    descriptor.supported_transform_mask =
        glasswyrm::output::kAllOutputTransformsMask;
    descriptor.minimum_scale = {1, 1};
    descriptor.maximum_scale = {4, 1};
    descriptor.maximum_scale_denominator = 120;
    descriptor.maximum_physical_width = 4096;
    descriptor.maximum_physical_height = 4096;
    descriptor.maximum_physical_pixels = 16'777'216;
    descriptor.modes.push_back(
        {mode_id, id, 640, 480, 60'000, 0, "640x480", true, true});
    layout.descriptors.emplace(id, std::move(descriptor));
    OutputState state;
    state.output_id = id;
    state.mode_id = mode_id;
    state.enabled = true;
    state.logical_x = static_cast<std::int32_t>(index * 640);
    state.logical_width = state.physical_width = 640;
    state.logical_height = state.physical_height = 480;
    state.refresh_millihertz = 60'000;
    state.scale = {1, 1};
    state.primary = index == 0;
    state.generation = 1;
    layout.states.emplace(id, state);
    layout.output_order.push_back(id);
  }
  layout.primary_output_id = {11};
  layout.root_logical_width = 1280;
  layout.root_logical_height = 480;
  layout.generation = 1;
  layout.enabled_output_count = 2;
  require(static_cast<bool>(glasswyrm::output::validate_layout(layout)),
          "control fixture layout is valid");
  return layout;
}

glasswyrm::server::LifecycleSnapshot window_snapshot() {
  glasswyrm::server::LifecycleSnapshot snapshot;
  glasswyrm::server::LifecycleWindow window;
  window.xid = 41;
  window.parent = 1;
  window.applied_x = 600;
  window.applied_y = 40;
  window.applied_width = 100;
  window.applied_height = 80;
  window.stacking = 0;
  window.policy_visible = true;
  window.focused = true;
  window.window_type = GWIPC_POLICY_WINDOW_NORMAL;
  window.applied_state = GWIPC_POLICY_APPLIED_FULLSCREEN;
  window.managed = true;
  window.decoration_eligible = true;
  window.fullscreen_eligible = GWIPC_TRI_STATE_TRUE;
  window.assigned_output_id = 11;
  window.output_memberships = {11, 12};
  window.scale.has_output_state = true;
  window.scale.preferred_scale_numerator = 1;
  window.scale.preferred_scale_denominator = 1;
  window.scale.accepted_buffer_scale = 2;
  window.scale.presentation =
      glasswyrm::server::WindowScalePresentationState::ScaleAwareActive;
  snapshot.windows.emplace(41, std::move(window));
  snapshot.root_order = {41};
  snapshot.focused_window = 41;
  return snapshot;
}

std::vector<gwipc_output_vrr_capability_upsert> vrr_capabilities() {
  std::vector<gwipc_output_vrr_capability_upsert> values;
  for (const std::uint64_t output_id : {11U, 12U}) {
    gwipc_output_vrr_capability_upsert value{};
    value.struct_size = sizeof(value);
    value.output_id = output_id;
    value.simulated = 1;
    value.reason_flags = GWIPC_VRR_REASON_SIMULATED_HEADLESS;
    values.push_back(value);
  }
  return values;
}

std::vector<gwipc_output_vrr_policy_upsert> vrr_policies() {
  std::vector<gwipc_output_vrr_policy_upsert> values;
  for (const std::uint64_t output_id : {11U, 12U}) {
    gwipc_output_vrr_policy_upsert value{};
    value.struct_size = sizeof(value);
    value.output_id = output_id;
    value.mode = GWIPC_VRR_POLICY_FOCUSED;
    values.push_back(value);
  }
  return values;
}

void make_vrr_ready(VrrStateCache &cache) {
  cache.set_window_preference(41, GWIPC_VRR_PREFERENCE_PREFER);
  std::vector<gwipc_policy_output_vrr_state> output_policies;
  for (const std::uint64_t output_id : {11U, 12U}) {
    gwipc_policy_output_vrr_state value{};
    value.struct_size = sizeof(value);
    value.output_id = output_id;
    value.mode = GWIPC_VRR_POLICY_FOCUSED;
    value.candidate_required = 1;
    if (output_id == 11) {
      value.selected_window_id = 41;
      value.desired_enabled = 1;
    }
    output_policies.push_back(value);
  }
  gwipc_policy_window_vrr_state window_policy{};
  window_policy.struct_size = sizeof(window_policy);
  window_policy.window_id = 41;
  window_policy.output_id = 11;
  window_policy.preference = GWIPC_VRR_PREFERENCE_PREFER;
  window_policy.selected = 1;
  window_policy.eligible = 1;
  window_policy.focused = 1;
  require(cache.stage_policy_result(1, output_policies, {window_policy}),
          "VRR query fixture stages policy state");

  std::vector<gwipc_output_vrr_state_upsert> output_states;
  for (const std::uint64_t output_id : {11U, 12U}) {
    gwipc_output_vrr_state_upsert value{};
    value.struct_size = sizeof(value);
    value.output_id = output_id;
    value.requested_mode = GWIPC_VRR_POLICY_FOCUSED;
    value.decision = output_id == 11 ? GWIPC_VRR_DECISION_ENABLED
                                     : GWIPC_VRR_DECISION_DISABLED;
    value.desired_enabled = output_id == 11;
    value.effective_enabled = output_id == 11;
    value.session_active = 1;
    value.candidate_window_id = output_id == 11 ? 41 : 0;
    value.candidate_surface_id =
        output_id == 11 ? (UINT64_C(1) << 32U) | 41U : 0;
    value.reason_flags = GWIPC_VRR_REASON_SIMULATED_HEADLESS |
                         (output_id == 11 ? 0 : GWIPC_VRR_REASON_NO_CANDIDATE);
    value.state_generation = 1;
    output_states.push_back(value);
  }
  require(cache.seed_compositor_state(output_states, {}),
          "VRR query fixture stages output state without timing");
  gwipc_surface_vrr_state window_state{};
  window_state.struct_size = sizeof(window_state);
  window_state.surface_id = (UINT64_C(1) << 32U) | 41U;
  window_state.window_id = 41;
  window_state.output_id = 11;
  window_state.preference = GWIPC_VRR_PREFERENCE_PREFER;
  window_state.policy_selected = 1;
  window_state.policy_eligible = 1;
  window_state.focused = 1;
  window_state.policy_generation = 1;
  require(cache.stage_surface_states({window_state}),
          "VRR query fixture stages window state");
}

Connection connect_tool(const std::string &path, const bool offer_vrr = false) {
  gwipc_connection_options options{};
  options.struct_size = sizeof(options);
  options.path = path.c_str();
  options.local_role = GWIPC_ROLE_DIAGNOSTIC_TOOL;
  options.acceptable_server_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_PROTOCOL_SERVER);
  options.offered_capabilities =
      GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE | GWIPC_CAP_OUTPUT_CONTROL |
      GWIPC_CAP_SURFACE_STATE | GWIPC_CAP_WINDOW_LIFECYCLE |
      GWIPC_CAP_SURFACE_OUTPUT_MEMBERSHIP | GWIPC_CAP_SCALE_METADATA;
  if (offer_vrr)
    options.offered_capabilities |= GWIPC_CAP_VRR_METADATA |
                                    GWIPC_CAP_VRR_POLICY |
                                    GWIPC_CAP_PRESENTATION_TIMING;
  options.required_peer_capabilities = GWIPC_CAP_OUTPUT_CONTROL;
  options.maximum_payload = 4096;
  options.maximum_fd_count = 0;
  options.maximum_queued_bytes = 2U * 1024U * 1024U;
  options.maximum_queued_messages = 2048;
  options.instance_label = "output-control-test";
  gwipc_connection *raw = nullptr;
  const auto status = gwipc_connection_connect(&options, &raw);
  require(status == GWIPC_STATUS_OK || status == GWIPC_STATUS_IN_PROGRESS,
          "diagnostic tool begins connecting");
  return Connection(raw);
}

void pump(OutputControlPeer &server,
          const std::vector<gwipc_connection *> &clients) {
  auto tagged = server.poll_descriptors();
  std::vector<pollfd> descriptors;
  descriptors.reserve(tagged.size() + clients.size());
  for (const auto &entry : tagged)
    descriptors.push_back({entry.descriptor, entry.events, 0});
  for (const auto *client : clients)
    descriptors.push_back({gwipc_connection_fd(client),
                           gwipc_connection_wanted_poll_events(client), 0});
  require(::poll(descriptors.data(), descriptors.size(), 10) >= 0,
          "control transport poll succeeds");
  for (std::size_t index = 0; index < tagged.size(); ++index)
    tagged[index].revents = descriptors[index].revents;
  server.service(tagged);
  for (std::size_t index = 0; index < clients.size(); ++index) {
    const auto client_status = gwipc_connection_process_poll_events(
        clients[index], descriptors[tagged.size() + index].revents);
    if (client_status != GWIPC_STATUS_OK &&
        client_status != GWIPC_STATUS_WOULD_BLOCK)
      std::fprintf(
          stderr, "test: client transport status=%s state=%u\n",
          gwipc_status_string(client_status),
          static_cast<unsigned>(gwipc_connection_get_state(clients[index])));
  }
}

void establish(OutputControlPeer &server,
               const std::vector<gwipc_connection *> &clients) {
  for (unsigned attempt = 0; attempt < 200; ++attempt) {
    pump(server, clients);
    if (std::ranges::all_of(clients, [](const auto *client) {
          return gwipc_connection_get_state(client) ==
                 GWIPC_CONNECTION_ESTABLISHED;
        }))
      return;
  }
  require(false, "diagnostic tools establish");
}

template <typename Value>
void enqueue_contract(gwipc_connection *connection, const std::uint16_t type,
                      const std::uint32_t flags, const Value &value,
                      gwipc_status (*encode)(const Value *,
                                             gwipc_contract_payload **)) {
  gwipc_contract_payload *raw = nullptr;
  require(encode(&value, &raw) == GWIPC_STATUS_OK,
          "control contract encodes");
  std::unique_ptr<gwipc_contract_payload,
                  decltype(&gwipc_contract_payload_destroy)>
      payload(raw, gwipc_contract_payload_destroy);
  std::size_t size = 0;
  const auto *data = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.flags = flags;
  message.payload = data;
  message.payload_size = size;
  require(gwipc_connection_enqueue(connection, &message) == GWIPC_STATUS_OK,
          "control contract enqueues");
}

template <typename Value>
void enqueue_control(gwipc_connection *connection, const std::uint16_t type,
                     const Value &value,
                     gwipc_status (*encode)(const Value *,
                                            gwipc_control_payload **)) {
  gwipc_control_payload *raw = nullptr;
  require(encode(&value, &raw) == GWIPC_STATUS_OK, "snapshot control encodes");
  std::unique_ptr<gwipc_control_payload,
                  decltype(&gwipc_control_payload_destroy)>
      payload(raw, gwipc_control_payload_destroy);
  std::size_t size = 0;
  const auto *data = gwipc_control_payload_data(payload.get(), &size);
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.payload = data;
  message.payload_size = size;
  require(gwipc_connection_enqueue(connection, &message) == GWIPC_STATUS_OK,
          "snapshot control enqueues");
}

std::vector<std::uint16_t> receive_types(OutputControlPeer &server,
                                         gwipc_connection *client,
                                         const std::size_t expected,
                                         gwipc_output_configuration_result
                                             *acknowledgement_result = nullptr) {
  std::vector<std::uint16_t> types;
  for (unsigned attempt = 0; attempt < 300 && types.size() < expected;
       ++attempt) {
    pump(server, {client});
    while (true) {
      gwipc_message *raw = nullptr;
      const auto status = gwipc_connection_receive(client, &raw);
      if (status == GWIPC_STATUS_WOULD_BLOCK)
        break;
      require(status == GWIPC_STATUS_OK, "tool receives control reply");
      std::unique_ptr<gwipc_message, decltype(&gwipc_message_destroy)> message(
          raw, gwipc_message_destroy);
      types.push_back(gwipc_message_type(message.get()));
      if (acknowledgement_result &&
          gwipc_message_type(message.get()) ==
              GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED) {
        gwipc_decoded_contract *raw_contract = nullptr;
        require(gwipc_contract_decode_message(message.get(), &raw_contract) ==
                    GWIPC_STATUS_OK,
                "configuration acknowledgement decodes");
        std::unique_ptr<gwipc_decoded_contract,
                        decltype(&gwipc_decoded_contract_destroy)>
            contract(raw_contract, gwipc_decoded_contract_destroy);
        const auto *value =
            gwipc_decoded_output_configuration_acknowledged(contract.get());
        require(value != nullptr, "configuration acknowledgement is typed");
        *acknowledgement_result = value->result;
      }
    }
  }
  return types;
}

gwipc_output_upsert state_record(const OutputState &state) {
  gwipc_output_upsert value{};
  value.struct_size = sizeof(value);
  value.output_id = state.output_id.value;
  value.enabled = state.enabled;
  value.logical_x = state.logical_x;
  value.logical_y = state.logical_y;
  value.logical_width = state.logical_width;
  value.logical_height = state.logical_height;
  value.physical_pixel_width = state.physical_width;
  value.physical_pixel_height = state.physical_height;
  value.refresh_millihertz = state.refresh_millihertz;
  value.scale_numerator = 1;
  value.scale_denominator = 1;
  value.transform = GWIPC_TRANSFORM_NORMAL;
  value.color = {GWIPC_SDR_COLOR_SPACE_SRGB, GWIPC_TRANSFER_FUNCTION_SRGB,
                 GWIPC_COLOR_PRIMARIES_SRGB, 0, 0, 0, 0};
  return value;
}

} // namespace

int main() {
  std::string directory = "/tmp/glasswyrm-output-control-XXXXXX";
  require(::mkdtemp(directory.data()) != nullptr,
          "create output-control directory");
  const std::string path = directory + "/control.sock";
  VrrStateCache vrr;
  require(vrr.replace_inventory(vrr_capabilities(), vrr_policies()),
          "install initial VRR query inventory");
  OutputControlPeer server(
      path, inventory(), [] { return window_snapshot(); }, &vrr);
  std::string error;
  const bool started = server.start(error);
  require(started, "start output-control listener: " + error);
  struct stat status{};
  require(::lstat(path.c_str(), &status) == 0 &&
              (status.st_mode & 0777U) == 0600U,
          "output-control listener is mode 0600");

  auto tool = connect_tool(path);
  establish(server, {tool.get()});
  require(server.peer_count() == 1, "server tracks the established tool");

  gwipc_output_state_query query{};
  query.struct_size = sizeof(query);
  query.query_id = 51;
  query.flags = GWIPC_OUTPUT_QUERY_LAYOUT;
  enqueue_contract(tool.get(), GWIPC_MESSAGE_OUTPUT_STATE_QUERY,
                   GWIPC_FLAG_ACK_REQUIRED, query,
                   gwipc_contract_encode_output_state_query);
  const auto query_types = receive_types(server, tool.get(), 5);
  require(query_types == std::vector<std::uint16_t>{
                             GWIPC_MESSAGE_SNAPSHOT_BEGIN,
                             GWIPC_MESSAGE_OUTPUT_UPSERT,
                             GWIPC_MESSAGE_OUTPUT_UPSERT,
                             GWIPC_MESSAGE_SNAPSHOT_END,
                             GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED},
          "read-only query returns one correlated atomic layout snapshot");

  query.query_id = 52;
  query.flags = GWIPC_OUTPUT_QUERY_WINDOWS;
  enqueue_contract(tool.get(), GWIPC_MESSAGE_OUTPUT_STATE_QUERY,
                   GWIPC_FLAG_ACK_REQUIRED, query,
                   gwipc_contract_encode_output_state_query);
  const auto window_types = receive_types(server, tool.get(), 6);
  require(window_types == std::vector<std::uint16_t>{
                              GWIPC_MESSAGE_SNAPSHOT_BEGIN,
                              GWIPC_MESSAGE_SURFACE_UPSERT,
                              GWIPC_MESSAGE_SURFACE_POLICY_UPSERT,
                              GWIPC_MESSAGE_SURFACE_OUTPUT_STATE,
                              GWIPC_MESSAGE_SNAPSHOT_END,
                              GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED},
          "window query publishes geometry, policy, membership, and scale");

  auto vrr_tool = connect_tool(path, true);
  establish(server, {tool.get(), vrr_tool.get()});
  query.query_id = 53;
  query.flags = GWIPC_OUTPUT_QUERY_VRR | GWIPC_OUTPUT_QUERY_WINDOWS;
  enqueue_contract(vrr_tool.get(), GWIPC_MESSAGE_OUTPUT_STATE_QUERY,
                   GWIPC_FLAG_ACK_REQUIRED, query,
                   gwipc_contract_encode_output_state_query);
  auto query_result = GWIPC_OUTPUT_CONFIGURATION_ACCEPTED;
  const auto busy_types =
      receive_types(server, vrr_tool.get(), 1, &query_result);
  require(busy_types ==
                  std::vector<std::uint16_t>{
                      GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED} &&
              query_result == GWIPC_OUTPUT_CONFIGURATION_BUSY &&
              server.peer_count() == 2,
          "valid not-ready VRR query returns only BUSY and retains its peer");

  make_vrr_ready(vrr);
  query.query_id = 54;
  enqueue_contract(vrr_tool.get(), GWIPC_MESSAGE_OUTPUT_STATE_QUERY,
                   GWIPC_FLAG_ACK_REQUIRED, query,
                   gwipc_contract_encode_output_state_query);
  const auto ready_types = receive_types(server, vrr_tool.get(), 13);
  require(ready_types.size() == 13 &&
              ready_types.front() == GWIPC_MESSAGE_SNAPSHOT_BEGIN &&
              ready_types.back() ==
                  GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED &&
              std::ranges::count(ready_types, GWIPC_MESSAGE_SNAPSHOT_BEGIN) ==
                  1 &&
              server.peer_count() == 2,
          "first ready VRR query succeeds on the retained connection");
  for (std::uint64_t ordinal = 1; ordinal < 200; ++ordinal) {
    query.query_id = 100 + ordinal;
    enqueue_contract(vrr_tool.get(), GWIPC_MESSAGE_OUTPUT_STATE_QUERY,
                     GWIPC_FLAG_ACK_REQUIRED, query,
                     gwipc_contract_encode_output_state_query);
    const auto repeated = receive_types(server, vrr_tool.get(), 13);
    require(repeated.size() == 13 &&
                repeated.front() == GWIPC_MESSAGE_SNAPSHOT_BEGIN &&
                repeated.back() ==
                    GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED,
            "sequential VRR query completes on the persistent connection");
  }
  require(server.peer_count() == 2,
          "two hundred sequential VRR queries retain the same peer");

  auto unnegotiated = connect_tool(path);
  establish(server, {tool.get(), vrr_tool.get(), unnegotiated.get()});
  query.query_id = 55;
  query.flags = GWIPC_OUTPUT_QUERY_VRR;
  enqueue_contract(unnegotiated.get(), GWIPC_MESSAGE_OUTPUT_STATE_QUERY,
                   GWIPC_FLAG_ACK_REQUIRED, query,
                   gwipc_contract_encode_output_state_query);
  for (unsigned attempt = 0; attempt < 100 && server.peer_count() != 2;
       ++attempt)
    pump(server, {tool.get(), vrr_tool.get(), unnegotiated.get()});
  require(server.peer_count() == 2 &&
              gwipc_connection_get_state(vrr_tool.get()) ==
                  GWIPC_CONNECTION_ESTABLISHED,
          "VRR query without negotiated capabilities remains a strict failure");
  unnegotiated.reset();
  vrr_tool.reset();
  for (unsigned attempt = 0; attempt < 100 && server.peer_count() != 1;
       ++attempt)
    pump(server, {tool.get()});
  require(server.peer_count() == 1,
          "closing the VRR tool leaves the original peer isolated");

  constexpr std::uint64_t configuration = 77;
  gwipc_snapshot_begin begin{};
  begin.struct_size = sizeof(begin);
  begin.snapshot_id = configuration;
  begin.domain = GWIPC_SNAPSHOT_OUTPUTS;
  begin.generation = 1;
  begin.expected_item_count = 2;
  enqueue_control(tool.get(), GWIPC_MESSAGE_SNAPSHOT_BEGIN, begin,
                  gwipc_control_encode_snapshot_begin);
  const auto layout = inventory();
  for (const auto id : layout.output_order) {
    const auto output = state_record(layout.states.at(id));
    enqueue_contract(tool.get(), GWIPC_MESSAGE_OUTPUT_UPSERT,
                     GWIPC_FLAG_SNAPSHOT_ITEM, output,
                     gwipc_contract_encode_output_upsert);
  }
  gwipc_snapshot_end end{};
  end.struct_size = sizeof(end);
  end.snapshot_id = configuration;
  end.generation = 1;
  end.actual_item_count = 2;
  enqueue_control(tool.get(), GWIPC_MESSAGE_SNAPSHOT_END, end,
                  gwipc_control_encode_snapshot_end);
  gwipc_output_configuration_commit commit{};
  commit.struct_size = sizeof(commit);
  commit.configuration_id = configuration;
  commit.base_generation = 1;
  commit.primary_output_id = 11;
  enqueue_contract(tool.get(), GWIPC_MESSAGE_OUTPUT_CONFIGURATION_COMMIT,
                   GWIPC_FLAG_ACK_REQUIRED, commit,
                   gwipc_contract_encode_output_configuration_commit);
  for (unsigned attempt = 0; attempt < 100 &&
                             !server.coordinator().transaction();
       ++attempt)
    pump(server, {tool.get()});
  require(server.coordinator().transaction() != nullptr,
          "complete staged commit reaches the global coordinator");
  require(server.acknowledge_internal_error(),
          "unwired runtime seam rejects without faking acceptance");
  auto commit_result = GWIPC_OUTPUT_CONFIGURATION_ACCEPTED;
  const auto commit_types =
      receive_types(server, tool.get(), 1, &commit_result);
  require(commit_types == std::vector<std::uint16_t>{
                              GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED},
          "commit receives one correlated acknowledgement");
  require(commit_result == GWIPC_OUTPUT_CONFIGURATION_INTERNAL_ERROR,
          "commit is never reported accepted before runtime application");

  auto malformed = connect_tool(path);
  establish(server, {tool.get(), malformed.get()});
  gwipc_output_remove remove{};
  remove.struct_size = sizeof(remove);
  remove.output_id = 11;
  enqueue_contract(malformed.get(), GWIPC_MESSAGE_OUTPUT_REMOVE, 0, remove,
                   gwipc_contract_encode_output_remove);
  for (unsigned attempt = 0; attempt < 100 && server.peer_count() != 1;
       ++attempt)
    pump(server, {tool.get(), malformed.get()});
  require(server.peer_count() == 1 &&
              gwipc_connection_get_state(tool.get()) ==
                  GWIPC_CONNECTION_ESTABLISHED,
          "malformed peer is isolated without disturbing the healthy tool");

  tool.reset();
  malformed.reset();
  std::filesystem::remove_all(directory);
  return 0;
}
