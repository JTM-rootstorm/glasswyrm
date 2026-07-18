#include <glasswyrm/ipc.h>

#include "ipc/connection_internal.hpp"
#include "ipc/wire/control.hpp"
#include "ipc/wire/output_contract.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace {

using namespace gw::ipc;
using namespace gw::ipc::wire;
using gw::test::require;

constexpr std::uint64_t kOutputCapabilities =
    GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE |
    GWIPC_CAP_SCALE_METADATA | GWIPC_CAP_WINDOW_POLICY |
    GWIPC_CAP_OUTPUT_MANAGEMENT | GWIPC_CAP_MULTI_OUTPUT_POLICY |
    GWIPC_CAP_SURFACE_OUTPUT_MEMBERSHIP | GWIPC_CAP_OUTPUT_CONTROL;

struct TestConnection final : gwipc_connection {
  TestConnection(const gwipc_role local, const gwipc_role remote,
                 const std::uint64_t capabilities) {
    state = GWIPC_CONNECTION_ESTABLISHED;
    config.local_role = local;
    config.maximum_payload = GWIPC_DEFAULT_MAXIMUM_PAYLOAD;
    config.maximum_fd_count = GWIPC_DEFAULT_MAXIMUM_FDS;
    config.maximum_queued_bytes = GWIPC_DEFAULT_MAXIMUM_QUEUED_BYTES;
    config.maximum_queued_messages = GWIPC_DEFAULT_MAXIMUM_QUEUED_MESSAGES;
    peer.role = remote;
    peer.capabilities = capabilities;
    peer.maximum_payload = GWIPC_DEFAULT_MAXIMUM_PAYLOAD;
    peer.maximum_fd_count = GWIPC_DEFAULT_MAXIMUM_FDS;
    peer.wire_version = {1, 0};
  }
};

TestConnection connection(const gwipc_role local, const gwipc_role peer,
                          const std::uint64_t capabilities) {
  return TestConnection{local, peer, capabilities};
}

SnapshotState snapshot(const SnapshotDomain domain) {
  return {true, 1, 1, UINT32_MAX, 0,
          static_cast<std::uint16_t>(domain), 0};
}

OutputDescriptorUpsert descriptor() {
  OutputDescriptorUpsert value;
  value.output_id = 1;
  value.capability_flags = kOutputConnected | kOutputArbitraryHeadlessMode |
                           kOutputScaleConfigurable |
                           kOutputTransformConfigurable |
                           kOutputPrimaryEligible;
  value.name = "HEADLESS-1";
  return value;
}

OutputModeUpsert mode() {
  return {1, 2, 640, 480, 60'000, true, true, 0};
}

SurfaceOutputState surface_output() {
  return {10, 1, {1}, 1, 1, 1, SurfaceScaleMode::Legacy, 1, 0};
}

PolicyOutputUpsert policy_output() {
  PolicyOutputUpsert value;
  value.output_id = 1;
  value.logical_width = 640;
  value.logical_height = 480;
  value.work_width = 640;
  value.work_height = 480;
  value.enabled = true;
  value.primary = true;
  return value;
}

PolicyWindowOutputHint policy_hint() { return {20, 0, 1, 0}; }

OutputConfigurationAcknowledged acknowledged(const std::uint64_t id) {
  return {id, 2, OutputConfigurationResult::Accepted, 0, 1, 640, 480, 1};
}

gwipc_status validate(gwipc_connection& value, const std::uint16_t type,
                      const std::uint32_t flags,
                      const std::span<const std::uint8_t> payload,
                      SnapshotState& state, const MessageDirection direction,
                      const std::span<const int> fds = {}) {
  return validate_application(value, type, flags, payload, fds, state,
                              direction);
}

void test_inventory_surface_and_policy_directions() {
  auto compositor = connection(GWIPC_ROLE_COMPOSITOR,
                               GWIPC_ROLE_PROTOCOL_SERVER,
                               kOutputCapabilities);
  auto outputs = snapshot(SnapshotDomain::Outputs);
  require(validate(compositor, GWIPC_MESSAGE_OUTPUT_DESCRIPTOR_UPSERT,
                   GWIPC_FLAG_SNAPSHOT_ITEM, encode(descriptor()), outputs,
                   MessageDirection::Outgoing) == GWIPC_STATUS_OK,
          "compositor sends descriptor in an Outputs snapshot");
  outputs = snapshot(SnapshotDomain::Outputs);
  require(validate(compositor, GWIPC_MESSAGE_OUTPUT_MODE_UPSERT,
                   GWIPC_FLAG_SNAPSHOT_ITEM, encode(mode()), outputs,
                   MessageDirection::Outgoing) == GWIPC_STATUS_OK,
          "compositor sends mode in an Outputs snapshot");

  auto wrong_direction = connection(GWIPC_ROLE_PROTOCOL_SERVER,
                                    GWIPC_ROLE_COMPOSITOR,
                                    kOutputCapabilities);
  outputs = snapshot(SnapshotDomain::Outputs);
  require(validate(wrong_direction, GWIPC_MESSAGE_OUTPUT_DESCRIPTOR_UPSERT,
                   GWIPC_FLAG_SNAPSHOT_ITEM, encode(descriptor()), outputs,
                   MessageDirection::Outgoing) == GWIPC_STATUS_PROTOCOL_ERROR,
          "descriptor rejects the reverse compositor direction");
  compositor.peer.capabilities &= ~GWIPC_CAP_OUTPUT_MANAGEMENT;
  outputs = snapshot(SnapshotDomain::Outputs);
  require(validate(compositor, GWIPC_MESSAGE_OUTPUT_MODE_UPSERT,
                   GWIPC_FLAG_SNAPSHOT_ITEM, encode(mode()), outputs,
                   MessageDirection::Outgoing) ==
              GWIPC_STATUS_CAPABILITY_MISMATCH,
          "inventory records require OutputManagement");

  auto server_compositor = connection(GWIPC_ROLE_PROTOCOL_SERVER,
                                      GWIPC_ROLE_COMPOSITOR,
                                      kOutputCapabilities);
  auto complete = snapshot(SnapshotDomain::CompleteSession);
  require(validate(server_compositor, GWIPC_MESSAGE_SURFACE_OUTPUT_STATE,
                   GWIPC_FLAG_SNAPSHOT_ITEM, encode(surface_output()), complete,
                   MessageDirection::Outgoing) == GWIPC_STATUS_OK,
          "server sends membership in a complete scene snapshot");
  auto wrong_domain = snapshot(SnapshotDomain::Surfaces);
  require(validate(server_compositor, GWIPC_MESSAGE_SURFACE_OUTPUT_STATE,
                   GWIPC_FLAG_SNAPSHOT_ITEM, encode(surface_output()),
                   wrong_domain, MessageDirection::Outgoing) ==
              GWIPC_STATUS_PROTOCOL_ERROR,
          "membership rejects a non-complete snapshot");

  auto server_gwm = connection(GWIPC_ROLE_PROTOCOL_SERVER,
                               GWIPC_ROLE_WINDOW_MANAGER,
                               kOutputCapabilities);
  auto policy = snapshot(SnapshotDomain::WindowPolicy);
  require(validate(server_gwm, GWIPC_MESSAGE_POLICY_OUTPUT_UPSERT,
                   GWIPC_FLAG_SNAPSHOT_ITEM, encode(policy_output()), policy,
                   MessageDirection::Outgoing) == GWIPC_STATUS_OK,
          "server sends policy output to GWM");
  policy = snapshot(SnapshotDomain::WindowPolicy);
  require(validate(server_gwm, GWIPC_MESSAGE_POLICY_WINDOW_OUTPUT_HINT,
                   GWIPC_FLAG_SNAPSHOT_ITEM, encode(policy_hint()), policy,
                   MessageDirection::Outgoing) == GWIPC_STATUS_OK,
          "server sends output hint to GWM");
  server_gwm.peer.capabilities &= ~GWIPC_CAP_MULTI_OUTPUT_POLICY;
  policy = snapshot(SnapshotDomain::WindowPolicy);
  require(validate(server_gwm, GWIPC_MESSAGE_POLICY_OUTPUT_UPSERT,
                   GWIPC_FLAG_SNAPSHOT_ITEM, encode(policy_output()), policy,
                   MessageDirection::Outgoing) ==
              GWIPC_STATUS_CAPABILITY_MISMATCH,
          "policy output requires MultiOutputPolicy");
}

void test_compositor_snapshot_domain_directions() {
  auto server = connection(GWIPC_ROLE_PROTOCOL_SERVER,
                           GWIPC_ROLE_COMPOSITOR, kOutputCapabilities);
  SnapshotState idle;
  require(validate(server, GWIPC_MESSAGE_SNAPSHOT_BEGIN, 0,
                   encode(SnapshotBegin{1, SnapshotDomain::CompleteSession, 0,
                                        1, 0}),
                   idle, MessageDirection::Outgoing) == GWIPC_STATUS_OK,
          "server starts only a complete-session compositor snapshot");
  idle = {};
  require(validate(server, GWIPC_MESSAGE_SNAPSHOT_BEGIN, 0,
                   encode(SnapshotBegin{2, SnapshotDomain::Outputs, 0, 1, 0}),
                   idle, MessageDirection::Outgoing) ==
              GWIPC_STATUS_PROTOCOL_ERROR,
          "server cannot inject an Outputs snapshot into the compositor");

  auto compositor = connection(GWIPC_ROLE_COMPOSITOR,
                               GWIPC_ROLE_PROTOCOL_SERVER,
                               kOutputCapabilities);
  idle = {};
  require(validate(compositor, GWIPC_MESSAGE_SNAPSHOT_BEGIN, 0,
                   encode(SnapshotBegin{3, SnapshotDomain::Outputs, 0, 1, 0}),
                   idle, MessageDirection::Outgoing) == GWIPC_STATUS_OK,
          "compositor starts an output-inventory snapshot");
  idle = {};
  require(validate(
              compositor, GWIPC_MESSAGE_SNAPSHOT_BEGIN, 0,
              encode(SnapshotBegin{4, SnapshotDomain::CompleteSession, 0, 1,
                                   0}),
              idle, MessageDirection::Outgoing) == GWIPC_STATUS_PROTOCOL_ERROR,
          "compositor cannot send a complete-session snapshot to the server");
}

void test_control_directions_flags_and_zero_fds() {
  auto server_compositor = connection(GWIPC_ROLE_PROTOCOL_SERVER,
                                      GWIPC_ROLE_COMPOSITOR,
                                      kOutputCapabilities);
  SnapshotState idle;
  require(validate(server_compositor, GWIPC_MESSAGE_OUTPUT_STATE_QUERY,
                   GWIPC_FLAG_ACK_REQUIRED,
                   encode(OutputStateQuery{31, kQueryOutputDescriptors}), idle,
                   MessageDirection::Outgoing) == GWIPC_STATUS_OK,
          "server queries compositor inventory");

  auto tool_server = connection(GWIPC_ROLE_DIAGNOSTIC_TOOL,
                                GWIPC_ROLE_PROTOCOL_SERVER,
                                kOutputCapabilities);
  idle = {};
  require(validate(tool_server, GWIPC_MESSAGE_OUTPUT_STATE_QUERY,
                   GWIPC_FLAG_ACK_REQUIRED,
                   encode(OutputStateQuery{32, kQueryOutputLayout}), idle,
                   MessageDirection::Outgoing) == GWIPC_STATUS_OK,
          "diagnostic tool queries server output state");
  idle = {};
  require(validate(tool_server, GWIPC_MESSAGE_OUTPUT_CONFIGURATION_COMMIT,
                   GWIPC_FLAG_ACK_REQUIRED,
                   encode(OutputConfigurationCommit{33, 1, 1, 0}), idle,
                   MessageDirection::Outgoing) == GWIPC_STATUS_OK,
          "diagnostic tool submits output configuration");
  idle = {};
  require(validate(tool_server, GWIPC_MESSAGE_OUTPUT_CONFIGURATION_COMMIT, 0,
                   encode(OutputConfigurationCommit{33, 1, 1, 0}), idle,
                   MessageDirection::Outgoing) == GWIPC_STATUS_PROTOCOL_ERROR,
          "configuration requires exactly AckRequired");

  auto server_tool = connection(GWIPC_ROLE_PROTOCOL_SERVER,
                                GWIPC_ROLE_DIAGNOSTIC_TOOL,
                                kOutputCapabilities);
  idle = {};
  require(validate(server_tool,
                   GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED,
                   GWIPC_FLAG_REPLY, encode(acknowledged(33)), idle,
                   MessageDirection::Outgoing) == GWIPC_STATUS_OK,
          "server acknowledges tool request");

  const std::array descriptor_fd{0};
  idle = {};
  require(validate(tool_server, GWIPC_MESSAGE_OUTPUT_STATE_QUERY,
                   GWIPC_FLAG_ACK_REQUIRED,
                   encode(OutputStateQuery{34, kQueryOutputLayout}), idle,
                   MessageDirection::Outgoing, descriptor_fd) ==
              GWIPC_STATUS_PROTOCOL_ERROR,
          "M13 output records reject all attached descriptors");
  tool_server.peer.capabilities &= ~GWIPC_CAP_OUTPUT_CONTROL;
  idle = {};
  require(validate(tool_server, GWIPC_MESSAGE_OUTPUT_STATE_QUERY,
                   GWIPC_FLAG_ACK_REQUIRED,
                   encode(OutputStateQuery{35, kQueryOutputLayout}), idle,
                   MessageDirection::Outgoing) ==
              GWIPC_STATUS_CAPABILITY_MISMATCH,
          "tool requests require OutputControl");
}

void test_every_output_record_rejects_descriptors() {
  struct Case {
    gwipc_role local;
    gwipc_role peer;
    std::uint16_t type;
    std::uint32_t flags;
    std::vector<std::uint8_t> payload;
    bool snapshot_active;
    SnapshotDomain domain;
  };
  const std::array cases{
      Case{GWIPC_ROLE_COMPOSITOR, GWIPC_ROLE_PROTOCOL_SERVER,
           GWIPC_MESSAGE_OUTPUT_DESCRIPTOR_UPSERT, GWIPC_FLAG_SNAPSHOT_ITEM,
           encode(descriptor()), true, SnapshotDomain::Outputs},
      Case{GWIPC_ROLE_COMPOSITOR, GWIPC_ROLE_PROTOCOL_SERVER,
           GWIPC_MESSAGE_OUTPUT_MODE_UPSERT, GWIPC_FLAG_SNAPSHOT_ITEM,
           encode(mode()), true, SnapshotDomain::Outputs},
      Case{GWIPC_ROLE_PROTOCOL_SERVER, GWIPC_ROLE_COMPOSITOR,
           GWIPC_MESSAGE_SURFACE_OUTPUT_STATE, GWIPC_FLAG_SNAPSHOT_ITEM,
           encode(surface_output()), true, SnapshotDomain::CompleteSession},
      Case{GWIPC_ROLE_PROTOCOL_SERVER, GWIPC_ROLE_WINDOW_MANAGER,
           GWIPC_MESSAGE_POLICY_OUTPUT_UPSERT, GWIPC_FLAG_SNAPSHOT_ITEM,
           encode(policy_output()), true, SnapshotDomain::WindowPolicy},
      Case{GWIPC_ROLE_PROTOCOL_SERVER, GWIPC_ROLE_WINDOW_MANAGER,
           GWIPC_MESSAGE_POLICY_WINDOW_OUTPUT_HINT, GWIPC_FLAG_SNAPSHOT_ITEM,
           encode(policy_hint()), true, SnapshotDomain::WindowPolicy},
      Case{GWIPC_ROLE_DIAGNOSTIC_TOOL, GWIPC_ROLE_PROTOCOL_SERVER,
           GWIPC_MESSAGE_OUTPUT_STATE_QUERY, GWIPC_FLAG_ACK_REQUIRED,
           encode(OutputStateQuery{101, kQueryOutputLayout}), false,
           SnapshotDomain::Outputs},
      Case{GWIPC_ROLE_DIAGNOSTIC_TOOL, GWIPC_ROLE_PROTOCOL_SERVER,
           GWIPC_MESSAGE_OUTPUT_CONFIGURATION_COMMIT, GWIPC_FLAG_ACK_REQUIRED,
           encode(OutputConfigurationCommit{102, 1, 1, 0}), false,
           SnapshotDomain::Outputs},
      Case{GWIPC_ROLE_PROTOCOL_SERVER, GWIPC_ROLE_DIAGNOSTIC_TOOL,
           GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED, GWIPC_FLAG_REPLY,
           encode(acknowledged(102)), false, SnapshotDomain::Outputs},
  };
  const std::array descriptor_fd{0};
  for (const auto& test : cases) {
    auto peer = connection(test.local, test.peer, kOutputCapabilities);
    auto state = test.snapshot_active ? snapshot(test.domain) : SnapshotState{};
    require(validate(peer, test.type, test.flags, test.payload, state,
                     MessageDirection::Outgoing, descriptor_fd) ==
                GWIPC_STATUS_PROTOCOL_ERROR,
            "every API 0.8 output record has a zero-FD contract");
  }
}

gwipc_status enqueue(gwipc_connection& connection, const std::uint16_t type,
                     const std::uint32_t flags,
                     const std::span<const std::uint8_t> payload,
                     const std::uint64_t reply_to = 0) {
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.flags = flags;
  message.reply_to = reply_to;
  message.payload = payload.data();
  message.payload_size = payload.size();
  std::uint64_t sequence = 0;
  return enqueue_with_sequence(connection, message, sequence);
}

Envelope reply_envelope(const std::uint64_t reply_to) {
  Envelope envelope;
  envelope.type = MessageType::OutputConfigurationAcknowledged;
  envelope.flags = GWIPC_FLAG_REPLY;
  envelope.sequence = 1;
  envelope.reply_to = reply_to;
  return envelope;
}

void test_query_and_configuration_correlation() {
  auto query_connection = connection(GWIPC_ROLE_PROTOCOL_SERVER,
                                     GWIPC_ROLE_COMPOSITOR,
                                     kOutputCapabilities);
  require(enqueue(query_connection, GWIPC_MESSAGE_OUTPUT_STATE_QUERY,
                  GWIPC_FLAG_ACK_REQUIRED,
                  encode(OutputStateQuery{41, kQueryOutputDescriptors})) ==
                  GWIPC_STATUS_OK &&
              query_connection.pending_output_requests.at(1).request_id == 41,
          "query tracks semantic ID beside sequence");
  SnapshotBegin begin{};
  begin.snapshot_id = 2;
  begin.domain = SnapshotDomain::Outputs;
  begin.generation = 2;
  begin.expected_item_count = 1;
  require(validate_application(query_connection, GWIPC_MESSAGE_SNAPSHOT_BEGIN,
                               0, encode(begin), {},
                               query_connection.incoming_snapshot,
                               MessageDirection::Incoming) ==
                  GWIPC_STATUS_OK &&
              validate_application(
                  query_connection, GWIPC_MESSAGE_OUTPUT_DESCRIPTOR_UPSERT,
                  GWIPC_FLAG_SNAPSHOT_ITEM, encode(descriptor()), {},
                  query_connection.incoming_snapshot,
                  MessageDirection::Incoming) == GWIPC_STATUS_OK &&
              validate_application(
                  query_connection, GWIPC_MESSAGE_SNAPSHOT_END, 0,
                  encode(SnapshotEnd{2, 2, 1}), {},
                  query_connection.incoming_snapshot,
                  MessageDirection::Incoming) == GWIPC_STATUS_OK &&
              query_connection.pending_output_requests.contains(1),
          "inventory snapshot preserves pending query correlation until ack");
  auto envelope = reply_envelope(1);
  require(validate_incoming_reply(query_connection, envelope,
                                  encode(acknowledged(41))) ==
                  GWIPC_STATUS_OK &&
              query_connection.pending_output_requests.empty() &&
              query_connection.pending_replies.empty(),
          "matching query acknowledgement clears both correlations");

  auto configuration = connection(GWIPC_ROLE_DIAGNOSTIC_TOOL,
                                  GWIPC_ROLE_PROTOCOL_SERVER,
                                  kOutputCapabilities);
  require(enqueue(configuration, GWIPC_MESSAGE_OUTPUT_CONFIGURATION_COMMIT,
                  GWIPC_FLAG_ACK_REQUIRED,
                  encode(OutputConfigurationCommit{51, 1, 1, 0})) ==
                  GWIPC_STATUS_OK &&
              configuration.pending_output_requests.at(1).request_id == 51,
          "configuration tracks semantic ID beside sequence");
  envelope = reply_envelope(1);
  require(validate_incoming_reply(configuration, envelope,
                                  encode(acknowledged(51))) ==
              GWIPC_STATUS_OK,
          "matching configuration acknowledgement is accepted");

  auto mismatch = connection(GWIPC_ROLE_PROTOCOL_SERVER,
                             GWIPC_ROLE_COMPOSITOR, kOutputCapabilities);
  require(enqueue(mismatch, GWIPC_MESSAGE_OUTPUT_STATE_QUERY,
                  GWIPC_FLAG_ACK_REQUIRED,
                  encode(OutputStateQuery{61, kQueryOutputLayout})) ==
              GWIPC_STATUS_OK,
          "queue query before semantic mismatch");
  envelope = reply_envelope(1);
  require(validate_incoming_reply(mismatch, envelope,
                                  encode(acknowledged(62))) ==
                  GWIPC_STATUS_PROTOCOL_ERROR &&
              mismatch.state == GWIPC_CONNECTION_CLOSING,
          "mismatched request ID closes only the offending peer");
}

void test_incoming_reply_and_failure_rollback() {
  auto server = connection(GWIPC_ROLE_PROTOCOL_SERVER,
                           GWIPC_ROLE_DIAGNOSTIC_TOOL, kOutputCapabilities);
  Envelope request;
  request.type = MessageType::OutputStateQuery;
  request.flags = GWIPC_FLAG_ACK_REQUIRED;
  request.sequence = 7;
  require(track_incoming_request(
              server, request,
              encode(OutputStateQuery{71, kQueryOutputDescriptors})) ==
                  GWIPC_STATUS_OK &&
              server.incoming_output_requests.at(7).request_id == 71,
          "incoming query tracks request ID");
  require(enqueue(server, GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED,
                  GWIPC_FLAG_REPLY, encode(acknowledged(71)), 7) ==
                  GWIPC_STATUS_OK &&
              server.incoming_output_requests.empty(),
          "outgoing acknowledgement matches and clears incoming query");

  auto failed = connection(GWIPC_ROLE_PROTOCOL_SERVER,
                           GWIPC_ROLE_COMPOSITOR, kOutputCapabilities);
  failed.config.maximum_queued_bytes = wire::kEnvelopeSize;
  require(enqueue(failed, GWIPC_MESSAGE_OUTPUT_STATE_QUERY,
                  GWIPC_FLAG_ACK_REQUIRED,
                  encode(OutputStateQuery{81, kQueryOutputLayout})) ==
                  GWIPC_STATUS_LIMIT_EXCEEDED &&
              failed.pending_output_requests.empty(),
          "failed outgoing queue rolls semantic correlation back");

  auto incoming = connection(GWIPC_ROLE_PROTOCOL_SERVER,
                             GWIPC_ROLE_DIAGNOSTIC_TOOL, kOutputCapabilities);
  request.sequence = 9;
  require(track_incoming_request(
              incoming, request,
              encode(OutputStateQuery{91, kQueryOutputWindows})) ==
              GWIPC_STATUS_OK,
          "track incoming query before queue rollback");
  rollback_incoming_request(incoming, request);
  require(incoming.incoming_output_requests.empty(),
          "failed incoming queue rolls semantic correlation back");
}

}  // namespace

int main() {
  test_inventory_surface_and_policy_directions();
  test_compositor_snapshot_domain_directions();
  test_control_directions_flags_and_zero_fds();
  test_every_output_record_rejects_descriptors();
  test_query_and_configuration_correlation();
  test_incoming_reply_and_failure_rollback();
  return 0;
}
