#include "glasswyrmd/output_control_peer.hpp"

#include "glasswyrmd/output_control_windows.hpp"
#include "gwcomp/output_inventory_publisher.hpp"

#include <cstdio>
#include <limits>
#include <memory>
#include <poll.h>
#include <utility>

namespace glasswyrm::server {
namespace {

constexpr std::uint32_t kMaximumPayload = 4096;
constexpr std::uint32_t kMaximumQueuedBytes = 2U * 1024U * 1024U;
constexpr std::uint16_t kMaximumQueuedMessages = 2048;
constexpr gwipc_capabilities kVrrCapabilities =
    GWIPC_CAP_VRR_METADATA | GWIPC_CAP_VRR_POLICY |
    GWIPC_CAP_PRESENTATION_TIMING;

bool vrr_negotiated(const gwipc_connection* connection) noexcept {
  return connection &&
         (gwipc_connection_peer_info(connection).capabilities &
          kVrrCapabilities) == kVrrCapabilities;
}

struct DecodedControlDeleter {
  void operator()(gwipc_decoded_control *value) const noexcept {
    gwipc_decoded_control_destroy(value);
  }
};

struct DecodedContractDeleter {
  void operator()(gwipc_decoded_contract *value) const noexcept {
    gwipc_decoded_contract_destroy(value);
  }
};

using DecodedControl =
    std::unique_ptr<gwipc_decoded_control, DecodedControlDeleter>;
using DecodedContract =
    std::unique_ptr<gwipc_decoded_contract, DecodedContractDeleter>;

DecodedControl decode_control(const gwipc_message *message) {
  gwipc_decoded_control *raw = nullptr;
  if (gwipc_control_decode_message(message, &raw) != GWIPC_STATUS_OK)
    return {};
  return DecodedControl(raw);
}

DecodedContract decode_contract(const gwipc_message *message) {
  gwipc_decoded_contract *raw = nullptr;
  if (gwipc_contract_decode_message(message, &raw) != GWIPC_STATUS_OK)
    return {};
  return DecodedContract(raw);
}

gw::ipc::wire::SdrColorMetadata
wire_color(const gwipc_sdr_color_metadata &value) noexcept {
  return {static_cast<gw::ipc::wire::SdrColorSpace>(value.color_space),
          static_cast<gw::ipc::wire::TransferFunction>(value.transfer_function),
          static_cast<gw::ipc::wire::ColorPrimaries>(value.primaries),
          value.luminance_available != 0,
          value.minimum_luminance_millinit,
          value.maximum_luminance_millinit,
          value.max_frame_average_luminance_millinit};
}

gw::ipc::wire::OutputUpsert wire_output(const gwipc_output_upsert &value) {
  return {value.output_id,
          value.enabled != 0,
          value.logical_x,
          value.logical_y,
          value.logical_width,
          value.logical_height,
          value.physical_pixel_width,
          value.physical_pixel_height,
          value.refresh_millihertz,
          value.scale_numerator,
          value.scale_denominator,
          static_cast<gw::ipc::wire::Transform>(value.transform),
          wire_color(value.color)};
}

gwipc_output_configuration_acknowledged public_acknowledgement(
    const gw::ipc::wire::OutputConfigurationAcknowledged &value) noexcept {
  gwipc_output_configuration_acknowledged output{};
  output.struct_size = sizeof(output);
  output.request_id = value.request_id;
  output.applied_generation = value.applied_generation;
  output.result =
      static_cast<gwipc_output_configuration_result>(value.result);
  output.flags = value.flags;
  output.primary_output_id = value.primary_output_id;
  output.root_logical_width = value.root_logical_width;
  output.root_logical_height = value.root_logical_height;
  output.enabled_output_count = value.enabled_output_count;
  return output;
}

} // namespace

void OutputControlPeer::ListenerDeleter::operator()(
    gwipc_listener *value) const noexcept {
  gwipc_listener_destroy(value);
}

void OutputControlPeer::ConnectionDeleter::operator()(
    gwipc_connection *value) const noexcept {
  gwipc_connection_destroy(value);
}

OutputControlPeer::OutputControlPeer(std::string path,
                                     output::OutputLayout inventory,
                                     WindowSnapshotProvider windows,
                                     VrrStateCache* vrr)
    : path_(std::move(path)), window_snapshot_provider_(std::move(windows)),
      vrr_(vrr), coordinator_(
                     std::move(inventory),
                     [&] {
                       std::map<std::uint64_t, gwipc_vrr_policy_mode> values;
                       if (vrr)
                         for (const auto& [id, state] : vrr->outputs())
                           values.emplace(id, state.policy.mode);
                       return values;
                     }()) {}

OutputControlPeer::~OutputControlPeer() = default;

bool OutputControlPeer::start(std::string &error) {
  if (listener_ || !coordinator_.valid()) {
    error = listener_ ? "output control listener was already started"
                      : "output control inventory is invalid";
    return false;
  }
  gwipc_listener_options options{};
  options.struct_size = sizeof(options);
  options.path = path_.c_str();
  options.local_role = GWIPC_ROLE_PROTOCOL_SERVER;
  options.accepted_peer_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_DIAGNOSTIC_TOOL);
  options.offered_capabilities =
      GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE | GWIPC_CAP_OUTPUT_CONTROL |
      GWIPC_CAP_SURFACE_STATE | GWIPC_CAP_WINDOW_LIFECYCLE |
      GWIPC_CAP_SURFACE_OUTPUT_MEMBERSHIP | GWIPC_CAP_SCALE_METADATA |
      (vrr_ ? GWIPC_CAP_VRR_METADATA | GWIPC_CAP_VRR_POLICY |
                  GWIPC_CAP_PRESENTATION_TIMING
            : 0);
  options.required_peer_capabilities = GWIPC_CAP_OUTPUT_CONTROL;
  options.maximum_payload = kMaximumPayload;
  options.maximum_fd_count = 0;
  options.require_same_uid = 1;
  options.maximum_queued_bytes = kMaximumQueuedBytes;
  options.maximum_queued_messages = kMaximumQueuedMessages;
  options.instance_label = "glasswyrmd-output-control";
  gwipc_listener *raw = nullptr;
  const auto status = gwipc_listener_create(&options, &raw);
  listener_.reset(raw);
  if (status != GWIPC_STATUS_OK) {
    error = std::string("output control listener creation failed: ") +
            gwipc_status_string(status);
    return false;
  }
  std::fprintf(stderr, "glasswyrmd: output control listening socket=%s\n",
               path_.c_str());
  error.clear();
  return true;
}

std::vector<OutputControlPollDescriptor>
OutputControlPeer::poll_descriptors() const {
  std::vector<OutputControlPollDescriptor> result;
  result.reserve(peers_.size() + 1U);
  if (listener_) {
    result.push_back({OutputControlPollTag::ControlListener, 0,
                      gwipc_listener_fd(listener_.get()),
                      static_cast<short>(
                          peers_.size() < GWIPC_MAXIMUM_OUTPUT_CONTROL_PEERS
                              ? POLLIN
                              : 0),
                      0});
  }
  for (const auto &[id, peer] : peers_)
    result.push_back({OutputControlPollTag::ControlPeer, id,
                      gwipc_connection_fd(peer.connection.get()),
                      gwipc_connection_wanted_poll_events(
                          peer.connection.get()),
                      0});
  return result;
}

void OutputControlPeer::service(
    const std::span<const OutputControlPollDescriptor> descriptors) {
  for (const auto &descriptor : descriptors) {
    if (descriptor.tag == OutputControlPollTag::ControlListener) {
      if ((descriptor.revents & POLLIN) != 0)
        accept_peers();
      continue;
    }
    service_peer(descriptor.peer_id, descriptor.revents);
  }
}

void OutputControlPeer::accept_peers() {
  while (listener_ && peers_.size() < GWIPC_MAXIMUM_OUTPUT_CONTROL_PEERS) {
    gwipc_connection *raw = nullptr;
    const auto status = gwipc_listener_accept(listener_.get(), &raw);
    if (status == GWIPC_STATUS_WOULD_BLOCK)
      return;
    if (status != GWIPC_STATUS_OK)
      return;
    if (next_peer_id_ == 0) {
      gwipc_connection_destroy(raw);
      return;
    }
    const auto id = next_peer_id_++;
    Peer peer;
    peer.id = id;
    peer.connection.reset(raw);
    const auto info = gwipc_connection_peer_info(raw);
    peers_.emplace(id, std::move(peer));
    std::fprintf(stderr,
                 "glasswyrmd: output control peer connected id=%llu pid=%d "
                 "uid=%u\n",
                 static_cast<unsigned long long>(id), info.pid, info.uid);
  }
}

void OutputControlPeer::service_peer(const std::uint64_t peer_id,
                                     const short revents) {
  const auto found = peers_.find(peer_id);
  if (found == peers_.end())
    return;
  if (revents != 0 &&
      gwipc_connection_process_poll_events(found->second.connection.get(),
                                           revents) != GWIPC_STATUS_OK) {
    disconnect(peer_id);
    return;
  }
  const auto current = peers_.find(peer_id);
  if (current == peers_.end())
    return;
  drain(current->second);
  const auto after = peers_.find(peer_id);
  if (after != peers_.end() &&
      gwipc_connection_get_state(after->second.connection.get()) ==
          GWIPC_CONNECTION_CLOSED)
    disconnect(peer_id);
}

void OutputControlPeer::drain(Peer &peer) {
  while (peers_.contains(peer.id)) {
    gwipc_message *raw = nullptr;
    const auto status =
        gwipc_connection_receive(peer.connection.get(), &raw);
    if (status == GWIPC_STATUS_WOULD_BLOCK)
      return;
    if (status != GWIPC_STATUS_OK) {
      disconnect(peer.id);
      return;
    }
    std::unique_ptr<gwipc_message, decltype(&gwipc_message_destroy)> message(
        raw, gwipc_message_destroy);
    if (!consume(peer, message.get())) {
      disconnect(peer.id);
      return;
    }
  }
}

bool OutputControlPeer::consume(Peer &peer, const gwipc_message *message) {
  if (message == nullptr || gwipc_message_fd_count(message) != 0)
    return false;
  switch (gwipc_message_type(message)) {
  case GWIPC_MESSAGE_SNAPSHOT_BEGIN:
    return consume_begin(peer, message);
  case GWIPC_MESSAGE_OUTPUT_UPSERT:
    return consume_output(peer, message);
  case GWIPC_MESSAGE_OUTPUT_VRR_POLICY_UPSERT:
    return consume_vrr_policy(peer, message);
  case GWIPC_MESSAGE_SNAPSHOT_END:
    return consume_end(peer, message);
  case GWIPC_MESSAGE_OUTPUT_STATE_QUERY:
    return consume_query(peer, message);
  case GWIPC_MESSAGE_OUTPUT_CONFIGURATION_COMMIT:
    return consume_commit(peer, message);
  default:
    return false;
  }
}

bool OutputControlPeer::consume_begin(Peer &peer,
                                      const gwipc_message *message) {
  const auto decoded = decode_control(message);
  const auto *begin =
      decoded ? gwipc_decoded_snapshot_begin(decoded.get()) : nullptr;
  if (begin == nullptr || gwipc_message_flags(message) != 0 ||
      gwipc_message_reply_to(message) != 0 ||
      begin->domain != GWIPC_SNAPSHOT_OUTPUTS || begin->flags != 0 ||
      begin->snapshot_id == 0 || begin->generation == 0 ||
      begin->expected_item_count == 0 ||
      begin->expected_item_count > GWIPC_MAXIMUM_OUTPUTS * 2U ||
      peer.staged.reading || peer.staged.complete)
    return false;
  peer.staged = {};
  peer.staged.snapshot_id = begin->snapshot_id;
  peer.staged.generation = begin->generation;
  peer.staged.expected_count = begin->expected_item_count;
  peer.staged.outputs.reserve(begin->expected_item_count);
  peer.staged.reading = true;
  return true;
}

bool OutputControlPeer::consume_vrr_policy(
    Peer& peer, const gwipc_message* message) {
  if (!vrr_ || !vrr_negotiated(peer.connection.get()) ||
      !peer.staged.reading || peer.staged.complete ||
      peer.staged.outputs.size() + peer.staged.vrr_policies.size() >=
          peer.staged.expected_count ||
      gwipc_message_flags(message) != GWIPC_FLAG_SNAPSHOT_ITEM ||
      gwipc_message_reply_to(message) != 0)
    return false;
  const auto decoded = decode_contract(message);
  const auto* value = decoded
                          ? gwipc_decoded_output_vrr_policy_upsert(decoded.get())
                          : nullptr;
  if (!value) return false;
  peer.staged.vrr_policies.push_back(*value);
  return true;
}

bool OutputControlPeer::consume_output(Peer &peer,
                                       const gwipc_message *message) {
  if (!peer.staged.reading || peer.staged.complete ||
      peer.staged.outputs.size() >= peer.staged.expected_count ||
      gwipc_message_flags(message) != GWIPC_FLAG_SNAPSHOT_ITEM ||
      gwipc_message_reply_to(message) != 0)
    return false;
  const auto decoded = decode_contract(message);
  const auto *value =
      decoded ? gwipc_decoded_output_upsert(decoded.get()) : nullptr;
  if (value == nullptr)
    return false;
  peer.staged.outputs.push_back(wire_output(*value));
  return true;
}

bool OutputControlPeer::consume_end(Peer &peer,
                                    const gwipc_message *message) {
  const auto decoded = decode_control(message);
  const auto *end = decoded ? gwipc_decoded_snapshot_end(decoded.get())
                            : nullptr;
  if (end == nullptr || !peer.staged.reading || peer.staged.complete ||
      gwipc_message_flags(message) != 0 ||
      gwipc_message_reply_to(message) != 0 ||
      end->snapshot_id != peer.staged.snapshot_id ||
      end->generation != peer.staged.generation ||
      end->actual_item_count !=
          peer.staged.outputs.size() + peer.staged.vrr_policies.size() ||
      end->actual_item_count != peer.staged.expected_count)
    return false;
  peer.staged.actual_count = end->actual_item_count;
  peer.staged.reading = false;
  peer.staged.complete = true;
  return true;
}

bool OutputControlPeer::consume_query(Peer &peer,
                                      const gwipc_message *message) {
  const auto decoded = decode_contract(message);
  const auto *query =
      decoded ? gwipc_decoded_output_state_query(decoded.get()) : nullptr;
  const auto snapshot_id = take_snapshot_id();
  if (query == nullptr || snapshot_id == 0)
    return false;
  std::vector<glasswyrm::compositor::OutputInventoryWindow> windows;
  if ((query->flags & GWIPC_OUTPUT_QUERY_WINDOWS) != 0 &&
      window_snapshot_provider_) {
    auto built = build_output_control_windows(
        window_snapshot_provider_(), coordinator_.committed_layout());
    if (!built)
      return false;
    windows = std::move(*built);
  }

  std::vector<gwipc_output_vrr_capability_upsert> vrr_capabilities;
  std::vector<gwipc_output_vrr_policy_upsert> vrr_policies;
  std::vector<gwipc_output_vrr_state_upsert> vrr_states;
  std::vector<gwipc_presentation_timing> vrr_timings;
  std::vector<gwipc_surface_vrr_state> vrr_windows;
  std::optional<glasswyrm::compositor::OutputInventoryVrr> vrr_inventory;
  if ((query->flags & GWIPC_OUTPUT_QUERY_VRR) != 0) {
    if (!vrr_ || !vrr_negotiated(peer.connection.get()))
      return false;
    const auto& layout = coordinator_.committed_layout();
    const auto& committed_policies = coordinator_.committed_vrr_policies();
    vrr_capabilities.reserve(layout.output_order.size());
    vrr_policies.reserve(layout.output_order.size());
    vrr_states.reserve(layout.output_order.size());
    vrr_timings.reserve(layout.output_order.size());
    for (const auto output_id : layout.output_order) {
      const auto cached = vrr_->outputs().find(output_id.value);
      const auto mode = committed_policies.find(output_id.value);
      if (cached == vrr_->outputs().end() || mode == committed_policies.end() ||
          !cached->second.compositor_state)
        return false;
      vrr_capabilities.push_back(cached->second.capability);
      auto policy = cached->second.policy;
      policy.mode = mode->second;
      vrr_policies.push_back(policy);
      vrr_states.push_back(*cached->second.compositor_state);
      if (cached->second.timing)
        vrr_timings.push_back(*cached->second.timing);
    }
    if ((query->flags & GWIPC_OUTPUT_QUERY_WINDOWS) != 0) {
      vrr_windows.reserve(windows.size());
      for (const auto& window : windows) {
        const auto cached =
            vrr_->windows().find(window.surface.x11_window_id);
        if (cached == vrr_->windows().end() ||
            !cached->second.compositor_state)
          return false;
        vrr_windows.push_back(*cached->second.compositor_state);
      }
    }
    vrr_inventory.emplace(glasswyrm::compositor::OutputInventoryVrr{
        vrr_capabilities, vrr_policies, vrr_states, vrr_timings,
        vrr_windows});
  }
  const auto publication =
      glasswyrm::compositor::build_output_inventory_publication(
          *query, gwipc_message_sequence(message), snapshot_id,
          coordinator_.committed_layout(), windows,
          vrr_inventory ? &*vrr_inventory : nullptr);
  if (!publication)
    return false;
  for (const auto &item : publication.messages) {
    gwipc_outgoing_message outgoing{};
    outgoing.struct_size = sizeof(outgoing);
    outgoing.type = item.type;
    outgoing.flags = item.flags;
    outgoing.reply_to = item.reply_to;
    outgoing.payload = item.payload.data();
    outgoing.payload_size = item.payload.size();
    if (gwipc_connection_enqueue(peer.connection.get(), &outgoing) !=
        GWIPC_STATUS_OK)
      return false;
  }
  return true;
}

bool OutputControlPeer::consume_commit(Peer &peer,
                                       const gwipc_message *message) {
  const auto decoded = decode_contract(message);
  const auto *value = decoded
                          ? gwipc_decoded_output_configuration_commit(
                                decoded.get())
                          : nullptr;
  if (value == nullptr || pending_reply_)
    return false;
  const gw::ipc::wire::OutputConfigurationCommit request{
      value->configuration_id, value->base_generation,
      value->primary_output_id, value->flags};

  std::optional<gw::ipc::wire::OutputConfigurationAcknowledged> result;
  if (coordinator_.transaction()) {
    result = coordinator_.submit(request);
  } else if (peer.staged.complete) {
    const bool legacy_vrr_snapshot =
        vrr_ && peer.staged.vrr_policies.empty() &&
        !vrr_negotiated(peer.connection.get());
    const auto synthesized_count = static_cast<std::uint32_t>(
        legacy_vrr_snapshot ? coordinator_.committed_vrr_policies().size()
                            : 0U);
    const auto begin = coordinator_.begin_snapshot(
        peer.staged.snapshot_id,
        peer.staged.expected_count + synthesized_count);
    if (begin == OutputConfigurationSnapshotStatus::Accepted) {
      for (const auto &output : peer.staged.outputs)
        (void)coordinator_.stage_output(output);
      for (const auto& policy : peer.staged.vrr_policies)
        (void)coordinator_.stage_vrr_policy(policy);
      if (legacy_vrr_snapshot)
        for (const auto& [output_id, mode] :
             coordinator_.committed_vrr_policies()) {
          gwipc_output_vrr_policy_upsert policy{};
          policy.struct_size = sizeof(policy);
          policy.output_id = output_id;
          policy.mode = mode;
          (void)coordinator_.stage_vrr_policy(policy);
        }
      (void)coordinator_.end_snapshot(peer.staged.snapshot_id,
                                      peer.staged.actual_count +
                                          synthesized_count);
    }
    result = coordinator_.submit(request);
  } else {
    result = coordinator_.submit(request);
  }
  peer.staged = {};
  if (result)
    return enqueue_acknowledgement(peer, gwipc_message_sequence(message),
                                   *result);
  pending_reply_ = PendingReply{peer.id, gwipc_message_sequence(message),
                                value->configuration_id};
  return true;
}

bool OutputControlPeer::enqueue_acknowledgement(
    Peer &peer, const std::uint64_t reply_to,
    const gw::ipc::wire::OutputConfigurationAcknowledged &acknowledgement) {
  const auto value = public_acknowledgement(acknowledgement);
  gwipc_contract_payload *raw = nullptr;
  if (gwipc_contract_encode_output_configuration_acknowledged(&value, &raw) !=
      GWIPC_STATUS_OK)
    return false;
  std::unique_ptr<gwipc_contract_payload,
                  decltype(&gwipc_contract_payload_destroy)>
      payload(raw, gwipc_contract_payload_destroy);
  std::size_t size = 0;
  const auto *data = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message outgoing{};
  outgoing.struct_size = sizeof(outgoing);
  outgoing.type = GWIPC_MESSAGE_OUTPUT_CONFIGURATION_ACKNOWLEDGED;
  outgoing.flags = GWIPC_FLAG_REPLY;
  outgoing.reply_to = reply_to;
  outgoing.payload = data;
  outgoing.payload_size = size;
  return gwipc_connection_enqueue(peer.connection.get(), &outgoing) ==
         GWIPC_STATUS_OK;
}

bool OutputControlPeer::finish_transaction(
    std::optional<gw::ipc::wire::OutputConfigurationAcknowledged> result) {
  if (!result || !pending_reply_ ||
      result->request_id != pending_reply_->request_id)
    return false;
  const auto reply = *pending_reply_;
  pending_reply_.reset();
  const auto peer = peers_.find(reply.peer_id);
  if (peer == peers_.end())
    return true;
  if (!enqueue_acknowledgement(peer->second, reply.sequence, *result))
    disconnect(reply.peer_id);
  return true;
}

bool OutputControlPeer::transaction_owner_connected() const noexcept {
  return pending_reply_ && peers_.contains(pending_reply_->peer_id);
}

bool OutputControlPeer::acknowledge_policy_rejected() {
  const auto* transaction = coordinator_.transaction();
  const auto result =
      transaction && transaction->old_vrr_policies !=
                         transaction->proposed_vrr_policies
          ? gw::ipc::wire::OutputConfigurationResult::VrrPolicyRejected
          : gw::ipc::wire::OutputConfigurationResult::PolicyRejected;
  return finish_transaction(coordinator_.reject_policy(result));
}

bool OutputControlPeer::acknowledge_compositor_rejected(
    const gw::ipc::wire::OutputConfigurationResult result) {
  const auto* transaction = coordinator_.transaction();
  const auto reported =
      transaction && transaction->old_vrr_policies !=
                         transaction->proposed_vrr_policies
          ? gw::ipc::wire::OutputConfigurationResult::VrrPresenterRejected
          : result;
  return coordinator_.begin_rollback(reported);
}

bool OutputControlPeer::acknowledge_rollback(const bool succeeded) {
  return finish_transaction(coordinator_.finish_rollback(succeeded));
}

bool OutputControlPeer::acknowledge_committed() {
  return finish_transaction(coordinator_.commit());
}

bool OutputControlPeer::acknowledge_internal_error() {
  return finish_transaction(coordinator_.fail_internal());
}

void OutputControlPeer::disconnect(const std::uint64_t peer_id) noexcept {
  if (peers_.erase(peer_id) != 0)
    std::fprintf(stderr,
                 "glasswyrmd: output control peer disconnected id=%llu\n",
                 static_cast<unsigned long long>(peer_id));
}

std::uint64_t OutputControlPeer::take_snapshot_id() noexcept {
  if (next_snapshot_id_ == 0)
    return 0;
  const auto result = next_snapshot_id_;
  if (next_snapshot_id_ == std::numeric_limits<std::uint64_t>::max())
    next_snapshot_id_ = 0;
  else
    ++next_snapshot_id_;
  return result;
}

} // namespace glasswyrm::server
