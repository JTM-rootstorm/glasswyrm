#include "ipc/connection_internal.hpp"
#include "ipc/wire/output_contract.hpp"
#include "ipc/wire/vrr_contract.hpp"
#include "tests/helpers/test_support.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace {

using namespace gw::ipc;
using namespace gw::ipc::wire;
using gw::test::require;

constexpr std::uint64_t kOutput = 11;
constexpr std::uint64_t kSurface = 21;
constexpr std::uint32_t kWindow = 31;

SnapshotState snapshot(const SnapshotDomain domain) {
  SnapshotState value;
  value.active = true;
  value.id = 1;
  value.generation = 1;
  value.expected_count = UINT32_MAX;
  value.domain = static_cast<std::uint16_t>(domain);
  return value;
}

template <typename Value>
gwipc_status validate(const gwipc_role local_role, const gwipc_role peer_role,
                      const std::uint64_t capabilities,
                      const std::uint16_t type, const std::uint32_t flags,
                      const Value& value, SnapshotState state = {},
                      const MessageDirection direction =
                          MessageDirection::Outgoing,
                      const std::span<const int> fds = {}) {
  gwipc_connection connection;
  connection.config.local_role = local_role;
  connection.peer.role = peer_role;
  connection.peer.capabilities = capabilities;
  const auto payload = encode(value);
  return validate_application(connection, type, flags, payload, fds, state,
                              direction);
}

OutputVrrCapabilityUpsert capability() {
  OutputVrrCapabilityUpsert value;
  value.output_id = kOutput;
  value.connector_property_present = true;
  value.hardware_capable = true;
  value.kms_controllable = true;
  return value;
}

OutputVrrStateUpsert output_state() {
  OutputVrrStateUpsert value;
  value.output_id = kOutput;
  value.requested_mode = VrrPolicyMode::Fullscreen;
  value.decision = VrrDecision::Enabled;
  value.desired_enabled = true;
  value.effective_enabled = true;
  value.property_readback_valid = true;
  value.session_active = true;
  value.candidate_window_id = kWindow;
  value.candidate_surface_id = kSurface;
  value.state_generation = 1;
  value.last_commit_id = 2;
  value.last_presented_generation = 3;
  return value;
}

SurfaceVrrState surface_state() {
  SurfaceVrrState value;
  value.surface_id = kSurface;
  value.window_id = kWindow;
  value.output_id = kOutput;
  value.preference = VrrWindowPreference::Prefer;
  value.policy_selected = true;
  value.policy_eligible = true;
  value.focused = true;
  value.fullscreen = true;
  value.exclusive_output_membership = true;
  value.policy_generation = 1;
  return value;
}

PresentationTiming timing() {
  PresentationTiming value;
  value.output_id = kOutput;
  value.commit_id = 2;
  value.presented_generation = 3;
  value.flip_sequence = 4;
  value.kernel_timestamp_nanoseconds = 5;
  value.interval_nanoseconds = 6;
  value.effective_vrr_enabled = true;
  value.timestamp_available = true;
  return value;
}

void compositor_and_diagnostic_records() {
  constexpr auto metadata = GWIPC_CAP_VRR_METADATA | GWIPC_CAP_VRR_POLICY;
  constexpr auto diagnostic = metadata | GWIPC_CAP_OUTPUT_CONTROL;
  require(validate(GWIPC_ROLE_COMPOSITOR, GWIPC_ROLE_PROTOCOL_SERVER,
                   metadata, GWIPC_MESSAGE_OUTPUT_VRR_CAPABILITY_UPSERT,
                   GWIPC_FLAG_SNAPSHOT_ITEM, capability(),
                   snapshot(SnapshotDomain::Outputs)) == GWIPC_STATUS_OK,
          "compositor capability is an output snapshot item");
  require(validate(GWIPC_ROLE_PROTOCOL_SERVER, GWIPC_ROLE_COMPOSITOR,
                   metadata, GWIPC_MESSAGE_OUTPUT_VRR_CAPABILITY_UPSERT,
                   GWIPC_FLAG_SNAPSHOT_ITEM, capability(),
                   snapshot(SnapshotDomain::Outputs),
                   MessageDirection::Incoming) == GWIPC_STATUS_OK,
          "incoming validation derives the compositor sender direction");
  require(validate(GWIPC_ROLE_PROTOCOL_SERVER, GWIPC_ROLE_DIAGNOSTIC_TOOL,
                   diagnostic, GWIPC_MESSAGE_OUTPUT_VRR_CAPABILITY_UPSERT,
                   GWIPC_FLAG_SNAPSHOT_ITEM, capability(),
                   snapshot(SnapshotDomain::Outputs)) == GWIPC_STATUS_OK,
          "server relays capability in a diagnostic output snapshot");

  const OutputVrrPolicyUpsert policy{kOutput, VrrPolicyMode::Focused, 0};
  require(validate(GWIPC_ROLE_PROTOCOL_SERVER, GWIPC_ROLE_COMPOSITOR,
                   GWIPC_CAP_VRR_POLICY,
                   GWIPC_MESSAGE_OUTPUT_VRR_POLICY_UPSERT,
                   GWIPC_FLAG_SNAPSHOT_ITEM, policy,
                   snapshot(SnapshotDomain::CompleteSession)) ==
              GWIPC_STATUS_OK,
          "server sends policy in the compositor session snapshot");
  require(validate(GWIPC_ROLE_DIAGNOSTIC_TOOL, GWIPC_ROLE_PROTOCOL_SERVER,
                   diagnostic, GWIPC_MESSAGE_OUTPUT_VRR_POLICY_UPSERT,
                   GWIPC_FLAG_SNAPSHOT_ITEM, policy,
                   snapshot(SnapshotDomain::Outputs)) == GWIPC_STATUS_OK &&
              validate(GWIPC_ROLE_PROTOCOL_SERVER,
                       GWIPC_ROLE_DIAGNOSTIC_TOOL, diagnostic,
                       GWIPC_MESSAGE_OUTPUT_VRR_POLICY_UPSERT,
                       GWIPC_FLAG_SNAPSHOT_ITEM, policy,
                       snapshot(SnapshotDomain::Outputs)) == GWIPC_STATUS_OK,
          "diagnostic configuration and query snapshots carry policy");

  require(validate(GWIPC_ROLE_COMPOSITOR, GWIPC_ROLE_PROTOCOL_SERVER,
                   metadata, GWIPC_MESSAGE_OUTPUT_VRR_STATE_UPSERT,
                   GWIPC_FLAG_REPLY, output_state()) == GWIPC_STATUS_OK &&
              validate(GWIPC_ROLE_COMPOSITOR,
                       GWIPC_ROLE_PROTOCOL_SERVER, metadata,
                       GWIPC_MESSAGE_OUTPUT_VRR_STATE_UPSERT,
                       GWIPC_FLAG_SNAPSHOT_ITEM, output_state(),
                       snapshot(SnapshotDomain::Outputs)) == GWIPC_STATUS_OK,
          "compositor state supports inventory and pre-ack runtime reply forms");
  require(validate(GWIPC_ROLE_PROTOCOL_SERVER, GWIPC_ROLE_DIAGNOSTIC_TOOL,
                   diagnostic, GWIPC_MESSAGE_OUTPUT_VRR_STATE_UPSERT,
                   GWIPC_FLAG_SNAPSHOT_ITEM, output_state(),
                   snapshot(SnapshotDomain::Outputs)) == GWIPC_STATUS_OK,
          "server relays effective state in a diagnostic snapshot");

  require(validate(GWIPC_ROLE_PROTOCOL_SERVER, GWIPC_ROLE_COMPOSITOR,
                   metadata, GWIPC_MESSAGE_SURFACE_VRR_STATE,
                   GWIPC_FLAG_SNAPSHOT_ITEM, surface_state(),
                   snapshot(SnapshotDomain::CompleteSession)) ==
              GWIPC_STATUS_OK &&
              validate(GWIPC_ROLE_PROTOCOL_SERVER,
                       GWIPC_ROLE_DIAGNOSTIC_TOOL, diagnostic,
                       GWIPC_MESSAGE_SURFACE_VRR_STATE,
                       GWIPC_FLAG_SNAPSHOT_ITEM, surface_state(),
                       snapshot(SnapshotDomain::Outputs)) == GWIPC_STATUS_OK,
          "surface state follows scene and diagnostic snapshot directions");

  require(validate(GWIPC_ROLE_COMPOSITOR, GWIPC_ROLE_PROTOCOL_SERVER,
                   GWIPC_CAP_PRESENTATION_TIMING,
                   GWIPC_MESSAGE_PRESENTATION_TIMING, 0, timing()) ==
              GWIPC_STATUS_OK &&
              validate(GWIPC_ROLE_PROTOCOL_SERVER,
                       GWIPC_ROLE_DIAGNOSTIC_TOOL,
                       GWIPC_CAP_OUTPUT_CONTROL |
                           GWIPC_CAP_PRESENTATION_TIMING,
                       GWIPC_MESSAGE_PRESENTATION_TIMING,
                       GWIPC_FLAG_SNAPSHOT_ITEM, timing(),
                       snapshot(SnapshotDomain::Outputs)) == GWIPC_STATUS_OK,
          "runtime timing is unflagged and diagnostic timing is a snapshot item");
}

void window_policy_records() {
  constexpr auto caps = GWIPC_CAP_WINDOW_POLICY | GWIPC_CAP_VRR_POLICY;
  const PolicyWindowVrrUpsert window_input{
      kWindow, VrrWindowPreference::Prefer, 0};
  const PolicyOutputVrrUpsert output_input{
      kOutput, VrrPolicyMode::Fullscreen, true, true, 0};
  const PolicyWindowVrrState window_result{
      kWindow, kOutput, VrrWindowPreference::Prefer, true, true, true, true,
      false, true, 0, 0};
  const PolicyOutputVrrState output_result{
      kOutput, VrrPolicyMode::Fullscreen, kWindow, true, true, 0, 0};
  require(validate(GWIPC_ROLE_PROTOCOL_SERVER, GWIPC_ROLE_WINDOW_MANAGER, caps,
                   GWIPC_MESSAGE_POLICY_WINDOW_VRR_UPSERT,
                   GWIPC_FLAG_SNAPSHOT_ITEM, window_input,
                   snapshot(SnapshotDomain::WindowPolicy)) == GWIPC_STATUS_OK &&
              validate(GWIPC_ROLE_PROTOCOL_SERVER,
                       GWIPC_ROLE_WINDOW_MANAGER, caps,
                       GWIPC_MESSAGE_POLICY_OUTPUT_VRR_UPSERT,
                       GWIPC_FLAG_SNAPSHOT_ITEM, output_input,
                       snapshot(SnapshotDomain::WindowPolicy)) ==
                  GWIPC_STATUS_OK,
          "server VRR inputs are WindowPolicy snapshot items");
  require(validate(GWIPC_ROLE_WINDOW_MANAGER, GWIPC_ROLE_PROTOCOL_SERVER, caps,
                   GWIPC_MESSAGE_POLICY_WINDOW_VRR_STATE,
                   GWIPC_FLAG_SNAPSHOT_ITEM, window_result,
                   snapshot(SnapshotDomain::WindowPolicy)) == GWIPC_STATUS_OK &&
              validate(GWIPC_ROLE_WINDOW_MANAGER,
                       GWIPC_ROLE_PROTOCOL_SERVER, caps,
                       GWIPC_MESSAGE_POLICY_OUTPUT_VRR_STATE,
                       GWIPC_FLAG_SNAPSHOT_ITEM, output_result,
                       snapshot(SnapshotDomain::WindowPolicy)) ==
                  GWIPC_STATUS_OK,
          "window-manager VRR results are WindowPolicy snapshot items");
}

void rejection_boundaries() {
  constexpr auto metadata = GWIPC_CAP_VRR_METADATA | GWIPC_CAP_VRR_POLICY;
  require(validate(GWIPC_ROLE_PROTOCOL_SERVER, GWIPC_ROLE_COMPOSITOR, metadata,
                   GWIPC_MESSAGE_OUTPUT_VRR_CAPABILITY_UPSERT,
                   GWIPC_FLAG_SNAPSHOT_ITEM, capability(),
                   snapshot(SnapshotDomain::Outputs)) ==
              GWIPC_STATUS_PROTOCOL_ERROR,
          "capability rejects the reverse role direction");
  require(validate(GWIPC_ROLE_PROTOCOL_SERVER, GWIPC_ROLE_COMPOSITOR,
                   GWIPC_CAP_VRR_POLICY,
                   GWIPC_MESSAGE_OUTPUT_VRR_POLICY_UPSERT,
                   GWIPC_FLAG_SNAPSHOT_ITEM,
                   OutputVrrPolicyUpsert{kOutput, VrrPolicyMode::Off, 0},
                   snapshot(SnapshotDomain::Outputs)) ==
              GWIPC_STATUS_PROTOCOL_ERROR,
          "compositor policy rejects the output-control snapshot domain");
  require(validate(GWIPC_ROLE_COMPOSITOR, GWIPC_ROLE_PROTOCOL_SERVER, metadata,
                   GWIPC_MESSAGE_OUTPUT_VRR_STATE_UPSERT, 0,
                   output_state()) == GWIPC_STATUS_PROTOCOL_ERROR &&
              validate(GWIPC_ROLE_COMPOSITOR,
                       GWIPC_ROLE_PROTOCOL_SERVER,
                       GWIPC_CAP_PRESENTATION_TIMING,
                       GWIPC_MESSAGE_PRESENTATION_TIMING,
                       GWIPC_FLAG_REPLY, timing()) ==
                  GWIPC_STATUS_PROTOCOL_ERROR,
          "runtime state and timing require their exact flags");
  require(validate(GWIPC_ROLE_COMPOSITOR, GWIPC_ROLE_PROTOCOL_SERVER, 0,
                   GWIPC_MESSAGE_OUTPUT_VRR_CAPABILITY_UPSERT,
                   GWIPC_FLAG_SNAPSHOT_ITEM, capability(),
                   snapshot(SnapshotDomain::Outputs)) ==
              GWIPC_STATUS_CAPABILITY_MISMATCH,
          "VRR records require negotiated capabilities");

  int descriptor = -1;
  require(validate(GWIPC_ROLE_COMPOSITOR, GWIPC_ROLE_PROTOCOL_SERVER, metadata,
                   GWIPC_MESSAGE_OUTPUT_VRR_CAPABILITY_UPSERT,
                   GWIPC_FLAG_SNAPSHOT_ITEM, capability(),
                   snapshot(SnapshotDomain::Outputs),
                   MessageDirection::Outgoing,
                   std::span<const int>(&descriptor, 1)) ==
              GWIPC_STATUS_PROTOCOL_ERROR,
          "VRR records reject all file descriptors");

  gwipc_connection connection;
  connection.config.local_role = GWIPC_ROLE_COMPOSITOR;
  connection.peer.role = GWIPC_ROLE_PROTOCOL_SERVER;
  connection.peer.capabilities = metadata;
  auto bytes = encode(capability());
  bytes.pop_back();
  auto state = snapshot(SnapshotDomain::Outputs);
  require(validate_application(
              connection, GWIPC_MESSAGE_OUTPUT_VRR_CAPABILITY_UPSERT,
              GWIPC_FLAG_SNAPSHOT_ITEM, bytes, {}, state,
              MessageDirection::Outgoing) == GWIPC_STATUS_PROTOCOL_ERROR,
          "VRR records reject malformed payloads");

  OutputDescriptorUpsert descriptor_record;
  descriptor_record.output_id = kOutput;
  descriptor_record.name = "HEADLESS-1";
  require(validate(GWIPC_ROLE_COMPOSITOR, GWIPC_ROLE_PROTOCOL_SERVER,
                   GWIPC_CAP_OUTPUT_MANAGEMENT,
                   GWIPC_MESSAGE_OUTPUT_DESCRIPTOR_UPSERT,
                   GWIPC_FLAG_SNAPSHOT_ITEM, descriptor_record,
                   snapshot(SnapshotDomain::Outputs)) == GWIPC_STATUS_OK,
          "M13 output validation remains valid without VRR capabilities");

  require(validate(GWIPC_ROLE_PROTOCOL_SERVER, GWIPC_ROLE_COMPOSITOR, metadata,
                   GWIPC_MESSAGE_OUTPUT_VRR_POLICY_UPSERT,
                   GWIPC_FLAG_SNAPSHOT_ITEM,
                   OutputVrrPolicyUpsert{kOutput, VrrPolicyMode::Off, 0},
                   snapshot(SnapshotDomain::CompleteSession),
                   MessageDirection::Incoming) ==
              GWIPC_STATUS_PROTOCOL_ERROR,
          "incoming direction swaps sender and receiver roles");
}

void diagnostic_snapshot_enqueue_skips_frame_correlation() {
  constexpr auto capabilities = GWIPC_CAP_OUTPUT_CONTROL |
                                GWIPC_CAP_VRR_METADATA |
                                GWIPC_CAP_VRR_POLICY;
  gwipc_connection connection;
  connection.state = GWIPC_CONNECTION_ESTABLISHED;
  connection.config.local_role = GWIPC_ROLE_PROTOCOL_SERVER;
  connection.peer.role = GWIPC_ROLE_DIAGNOSTIC_TOOL;
  connection.peer.capabilities = capabilities;
  connection.peer.maximum_payload = GWIPC_DEFAULT_MAXIMUM_PAYLOAD;
  connection.peer.maximum_fd_count = GWIPC_DEFAULT_MAXIMUM_FDS;
  connection.outgoing_snapshot = snapshot(SnapshotDomain::Outputs);

  const auto payload = encode(output_state());
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = GWIPC_MESSAGE_OUTPUT_VRR_STATE_UPSERT;
  message.flags = GWIPC_FLAG_SNAPSHOT_ITEM;
  message.payload = payload.data();
  message.payload_size = payload.size();
  std::uint64_t sequence = 0;
  require(gwipc_connection_enqueue_with_sequence(&connection, &message,
                                                  &sequence) ==
              GWIPC_STATUS_OK &&
              sequence == 1 && connection.outgoing.size() == 1,
          "diagnostic VRR state snapshot does not require frame correlation");
}

}  // namespace

int main() {
  compositor_and_diagnostic_records();
  window_policy_records();
  rejection_boundaries();
  diagnostic_snapshot_enqueue_skips_frame_correlation();
  return 0;
}
