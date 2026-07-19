#include "gwm/contract_vrr.hpp"

#include "ipc/vrr_membership_hint.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace glasswyrm::wm::runtime {
namespace {

struct PayloadDelete {
  void operator()(gwipc_contract_payload* value) const {
    gwipc_contract_payload_destroy(value);
  }
};

gwipc_policy_output_vrr_state state_from(const VrrOutputState& value) {
  gwipc_policy_output_vrr_state state{};
  state.struct_size = sizeof(state);
  state.output_id = value.output_id;
  state.mode = static_cast<gwipc_vrr_policy_mode>(value.mode);
  state.selected_window_id = value.selected_window_id;
  state.desired_enabled = value.desired_enabled;
  state.candidate_required = value.candidate_required;
  state.reason_flags = value.reason_flags;
  state.flags = value.flags;
  return state;
}

gwipc_policy_window_vrr_state state_from(const VrrWindowState& value) {
  gwipc_policy_window_vrr_state state{};
  state.struct_size = sizeof(state);
  state.window_id = value.window_id;
  state.output_id = value.output_id;
  state.preference =
      static_cast<gwipc_vrr_window_preference>(value.preference);
  state.selected = value.selected;
  state.eligible = value.eligible;
  state.focused = value.focused;
  state.fullscreen = value.fullscreen;
  state.borderless_fullscreen = value.borderless_fullscreen;
  state.exclusive_output_membership = value.exclusive_output_membership;
  state.reason_flags = value.reason_flags;
  state.flags = value.flags;
  return state;
}

template <class Value, class Encoder>
bool account(const Value& value, Encoder encoder, std::size_t& bytes) {
  gwipc_contract_payload* raw = nullptr;
  if (encoder(&value, &raw) != GWIPC_STATUS_OK) return false;
  const std::unique_ptr<gwipc_contract_payload, PayloadDelete> payload(raw);
  std::size_t size = 0;
  (void)gwipc_contract_payload_data(payload.get(), &size);
  if (size > GWIPC_HARD_MAXIMUM_QUEUED_BYTES - bytes) return false;
  bytes += size;
  return true;
}

template <class Value, class Encoder>
bool enqueue(gwipc_connection* connection, const std::uint16_t type,
             const Value& value, Encoder encoder) {
  gwipc_contract_payload* raw = nullptr;
  if (encoder(&value, &raw) != GWIPC_STATUS_OK) return false;
  const std::unique_ptr<gwipc_contract_payload, PayloadDelete> payload(raw);
  std::size_t size = 0;
  const auto* data = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message outgoing{};
  outgoing.struct_size = sizeof(outgoing);
  outgoing.type = type;
  outgoing.flags = GWIPC_FLAG_SNAPSHOT_ITEM;
  outgoing.payload = data;
  outgoing.payload_size = size;
  return gwipc_connection_enqueue(connection, &outgoing) == GWIPC_STATUS_OK;
}

}  // namespace

bool negotiated_vrr_profile(const gwipc_connection* connection) noexcept {
  constexpr auto required = GWIPC_CAP_WINDOW_POLICY | GWIPC_CAP_VRR_POLICY |
                            GWIPC_CAP_MULTI_OUTPUT_POLICY |
                            GWIPC_CAP_SCALE_METADATA;
  return connection &&
         (gwipc_connection_peer_info(connection).capabilities & required) ==
             required;
}

gwipc_policy_result vrr_result_from(const VrrEvaluationError error) noexcept {
  switch (error) {
    case VrrEvaluationError::None: return GWIPC_POLICY_ACCEPTED;
    case VrrEvaluationError::IncompleteSnapshot:
      return GWIPC_POLICY_REJECTED_INCOMPLETE_SNAPSHOT;
    case VrrEvaluationError::BasePolicyMismatch:
    case VrrEvaluationError::InvalidOutput:
      return GWIPC_POLICY_REJECTED_INVALID_CONTEXT;
    case VrrEvaluationError::InvalidWindow:
      return GWIPC_POLICY_REJECTED_INVALID_WINDOW;
    case VrrEvaluationError::UnknownReference:
      return GWIPC_POLICY_REJECTED_UNKNOWN_REFERENCE;
    case VrrEvaluationError::Limit: return GWIPC_POLICY_REJECTED_LIMIT;
  }
  return GWIPC_POLICY_REJECTED_INVALID_WINDOW;
}

bool consume_vrr_contract(PeerState& peer,
                          const gwipc_connection* connection,
                          const gwipc_decoded_contract* contract,
                          const std::uint16_t type) {
  if (!negotiated_vrr_profile(connection) ||
      !peer.transaction.snapshot_active())
    return false;
  if (type == GWIPC_MESSAGE_POLICY_OUTPUT_VRR_UPSERT) {
    const auto* value = gwipc_decoded_policy_output_vrr_upsert(contract);
    if (!value || peer.pending_vrr.outputs.contains(value->output_id))
      return false;
    return peer.pending_vrr.outputs
        .emplace(value->output_id,
                 VrrOutputInput{
                     value->output_id, static_cast<VrrPolicyMode>(value->mode),
                     value->hardware_capable != 0,
                     value->kms_controllable != 0, value->flags})
        .second;
  }
  const auto* value = gwipc_decoded_policy_window_vrr_upsert(contract);
  if (type != GWIPC_MESSAGE_POLICY_WINDOW_VRR_UPSERT || !value ||
      peer.pending_vrr.windows.contains(value->window_id))
    return false;
  return peer.pending_vrr.windows
      .emplace(value->window_id,
               VrrWindowInput{
                   value->window_id,
                   static_cast<VrrWindowPreference>(value->preference), {},
                   value->flags})
      .second;
}

bool populate_vrr_memberships(const RawState& raw, VrrInputs& inputs) {
  std::vector<std::uint64_t> output_ids;
  output_ids.reserve(raw.outputs.size());
  for (const auto& [output_id, unused] : raw.outputs) {
    static_cast<void>(unused);
    output_ids.push_back(output_id);
  }
  if (!ipc::internal::valid_vrr_membership_output_order(output_ids))
    return false;
  for (auto& [window_id, input] : inputs.windows) {
    const auto hint = raw.output_hints.find(window_id);
    if (hint == raw.output_hints.end()) return false;
    const auto membership = ipc::internal::decode_vrr_membership_hint(
        output_ids, hint->second.preferred_output_id);
    if (!membership) return false;
    input.output_membership = *membership;
  }
  return true;
}

bool preflight_vrr_policy(const VrrPolicyState& policy,
                          std::size_t& queued_bytes) {
  for (const auto& [id, value] : policy.outputs) {
    static_cast<void>(id);
    if (!account(state_from(value),
                 gwipc_contract_encode_policy_output_vrr_state,
                 queued_bytes))
      return false;
  }
  for (const auto& [id, value] : policy.windows) {
    static_cast<void>(id);
    if (!account(state_from(value),
                 gwipc_contract_encode_policy_window_vrr_state,
                 queued_bytes))
      return false;
  }
  return true;
}

bool enqueue_vrr_policy(gwipc_connection* connection,
                        const VrrPolicyState& policy) {
  for (const auto& [id, value] : policy.outputs) {
    static_cast<void>(id);
    if (!enqueue(connection, GWIPC_MESSAGE_POLICY_OUTPUT_VRR_STATE,
                 state_from(value),
                 gwipc_contract_encode_policy_output_vrr_state))
      return false;
  }
  for (const auto& [id, value] : policy.windows) {
    static_cast<void>(id);
    if (!enqueue(connection, GWIPC_MESSAGE_POLICY_WINDOW_VRR_STATE,
                 state_from(value),
                 gwipc_contract_encode_policy_window_vrr_state))
      return false;
  }
  return true;
}

}  // namespace glasswyrm::wm::runtime
