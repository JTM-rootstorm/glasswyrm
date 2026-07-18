#include "gwm/contract_dispatch.hpp"

#include "wm/interactive_policy.hpp"

#include <glasswyrm/ipc.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>

namespace glasswyrm::wm::runtime {
namespace {

constexpr std::uint32_t kMaximumQueuedBytes = GWIPC_HARD_MAXIMUM_QUEUED_BYTES;
constexpr std::uint16_t kMaximumQueuedMessages = 8192;

struct ContractDeleter {
  void operator()(gwipc_decoded_contract* value) const {
    gwipc_decoded_contract_destroy(value);
  }
};
struct ControlDeleter {
  void operator()(gwipc_decoded_control* value) const {
    gwipc_decoded_control_destroy(value);
  }
};
struct ContractPayloadDeleter {
  void operator()(gwipc_contract_payload* value) const {
    gwipc_contract_payload_destroy(value);
  }
};
struct ControlPayloadDeleter {
  void operator()(gwipc_control_payload* value) const {
    gwipc_control_payload_destroy(value);
  }
};

glasswyrm::wm::Context context_from(const gwipc_policy_context_upsert& value) {
  return {value.root_window_id, value.workspace_id, value.output_id,
          value.work_x, value.work_y, value.work_width, value.work_height,
          value.flags};
}

glasswyrm::wm::RawWindow window_from(const gwipc_policy_window_upsert& value) {
  using glasswyrm::wm::DecorationPreference;
  using glasswyrm::wm::WindowType;
  return {value.window_id,
          value.parent_window_id,
          value.transient_for,
          value.workspace_id,
          value.requested_x,
          value.requested_y,
          value.requested_width,
          value.requested_height,
          value.border_width,
          static_cast<WindowType>(value.window_type),
          value.map_intent == GWIPC_POLICY_WANTS_MAP,
          value.override_redirect != 0,
          static_cast<DecorationPreference>(value.decoration_preference),
          value.fullscreen_requested != 0,
          value.maximized_requested != 0,
          value.minimized_requested != 0,
          value.attention_requested != 0,
          value.creation_serial,
          value.map_serial,
          value.focus_serial,
          value.flags};
}

glasswyrm::wm::RawWindow window_from(
    const gwipc_policy_lifecycle_window_upsert& value) {
  auto window = window_from(value.window);
  window.geometry_serial = value.geometry_serial;
  window.stack_serial = value.stack_serial;
  window.stack_sibling = value.stack_sibling;
  window.stack_mode = static_cast<glasswyrm::wm::StackMode>(value.stack_mode);
  return window;
}

glasswyrm::wm::OutputContext output_from(
    const gwipc_policy_output_upsert& value) {
  using glasswyrm::wm::OutputTransform;
  return {value.output_id,
          {value.logical_x, value.logical_y, value.logical_width,
           value.logical_height},
          {value.work_x, value.work_y, value.work_width, value.work_height},
          value.scale_numerator,
          value.scale_denominator,
          static_cast<OutputTransform>(value.transform),
          value.enabled != 0,
          value.primary != 0,
          value.flags};
}

glasswyrm::wm::WindowOutputHint hint_from(
    const gwipc_policy_window_output_hint& value) {
  return {value.window_id, value.previous_output_id, value.preferred_output_id,
          value.flags};
}

bool multi_output_profile(const gwipc_connection* connection) {
  constexpr auto required =
      GWIPC_CAP_MULTI_OUTPUT_POLICY | GWIPC_CAP_SCALE_METADATA;
  return (gwipc_connection_peer_info(connection).capabilities & required) ==
         required;
}

gwipc_policy_result result_from(glasswyrm::wm::EvaluationError error) {
  using glasswyrm::wm::EvaluationError;
  switch (error) {
    case EvaluationError::None: return GWIPC_POLICY_ACCEPTED;
    case EvaluationError::IncompleteSnapshot:
      return GWIPC_POLICY_REJECTED_INCOMPLETE_SNAPSHOT;
    case EvaluationError::InvalidContext:
      return GWIPC_POLICY_REJECTED_INVALID_CONTEXT;
    case EvaluationError::InvalidWindow:
      return GWIPC_POLICY_REJECTED_INVALID_WINDOW;
    case EvaluationError::UnknownReference:
      return GWIPC_POLICY_REJECTED_UNKNOWN_REFERENCE;
    case EvaluationError::Limit: return GWIPC_POLICY_REJECTED_LIMIT;
    case EvaluationError::UnsupportedMetadata:
      return GWIPC_POLICY_REJECTED_UNSUPPORTED_METADATA;
    case EvaluationError::OutputFailure: return GWIPC_POLICY_REJECTED_LIMIT;
  }
  return GWIPC_POLICY_REJECTED_INVALID_WINDOW;
}

gwipc_policy_window_state state_from(const glasswyrm::wm::WindowState& value) {
  gwipc_policy_window_state state{};
  state.struct_size = sizeof(state);
  state.window_id = value.window_id;
  state.transient_for = value.transient_for;
  state.workspace_id = value.workspace_id;
  state.output_id = value.output_id;
  state.final_x = value.final_x;
  state.final_y = value.final_y;
  state.final_width = value.final_width;
  state.final_height = value.final_height;
  state.stacking = value.stacking;
  state.window_type = static_cast<gwipc_policy_window_type>(value.window_type);
  state.applied_state =
      static_cast<gwipc_policy_applied_state>(value.applied_state);
  state.visible = value.visible;
  state.focused = value.focused;
  state.managed = value.managed;
  state.decoration_eligible = value.decoration_eligible;
  state.override_redirect = value.override_redirect;
  state.attention_requested = value.attention_requested;
  state.fullscreen_eligible =
      static_cast<gwipc_tri_state>(value.fullscreen_eligible);
  state.direct_scanout_eligible =
      static_cast<gwipc_tri_state>(value.direct_scanout_eligible);
  return state;
}

template <class Value, class Encoder>
bool enqueue_contract(gwipc_connection* connection, std::uint16_t type,
                      std::uint32_t flags, std::uint64_t reply_to,
                      const Value& value, Encoder encoder) {
  gwipc_contract_payload* raw = nullptr;
  if (encoder(&value, &raw) != GWIPC_STATUS_OK) return false;
  const std::unique_ptr<gwipc_contract_payload, ContractPayloadDeleter> payload(raw);
  std::size_t size = 0;
  const auto* bytes = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message outgoing{};
  outgoing.struct_size = sizeof(outgoing);
  outgoing.type = type;
  outgoing.flags = flags;
  outgoing.reply_to = reply_to;
  outgoing.payload = bytes;
  outgoing.payload_size = size;
  return gwipc_connection_enqueue(connection, &outgoing) == GWIPC_STATUS_OK;
}

template <class Value, class Encoder>
bool enqueue_control(gwipc_connection* connection, std::uint16_t type,
                     const Value& value, Encoder encoder) {
  gwipc_control_payload* raw = nullptr;
  if (encoder(&value, &raw) != GWIPC_STATUS_OK) return false;
  const std::unique_ptr<gwipc_control_payload, ControlPayloadDeleter> payload(raw);
  std::size_t size = 0;
  const auto* bytes = gwipc_control_payload_data(payload.get(), &size);
  gwipc_outgoing_message outgoing{};
  outgoing.struct_size = sizeof(outgoing);
  outgoing.type = type;
  outgoing.payload = bytes;
  outgoing.payload_size = size;
  return gwipc_connection_enqueue(connection, &outgoing) == GWIPC_STATUS_OK;
}

bool preflight_policy(const glasswyrm::wm::PolicyState& policy) {
  if (policy.windows.size() > kMaximumQueuedMessages - 3U) return false;
  std::size_t bytes = (policy.windows.size() + 3U) * 40U;
  for (const auto id : policy.output_order) {
    const auto state = state_from(policy.windows.at(id));
    gwipc_contract_payload* raw = nullptr;
    if (gwipc_contract_encode_policy_window_state(&state, &raw) !=
        GWIPC_STATUS_OK)
      return false;
    const std::unique_ptr<gwipc_contract_payload, ContractPayloadDeleter> payload(raw);
    std::size_t size = 0;
    (void)gwipc_contract_payload_data(payload.get(), &size);
    if (size > kMaximumQueuedBytes - bytes) return false;
    bytes += size;
  }
  return bytes <= kMaximumQueuedBytes;
}

gwipc_policy_bindings_upsert default_bindings() {
  const glasswyrm::wm::InteractiveBindings bindings;
  gwipc_policy_bindings_upsert result{};
  result.struct_size = sizeof(result);
  result.move_modifiers = bindings.move_modifiers;
  result.resize_modifiers = bindings.resize_modifiers;
  result.close_modifiers = bindings.close_modifiers;
  result.move_button = bindings.move_button;
  result.resize_button = bindings.resize_button;
  result.close_keysym = bindings.close_keysym;
  result.minimum_width = bindings.minimum_width;
  result.minimum_height = bindings.minimum_height;
  result.raise_on_focus = bindings.raise_on_focus;
  result.consume_wm_bindings = bindings.consume_wm_bindings;
  return result;
}

bool preflight_interactive_policy(const glasswyrm::wm::PolicyState& policy) {
  if (!preflight_policy(policy)) return false;
  return policy.windows.size() + 4U <= kMaximumQueuedMessages;
}

bool enqueue_policy(gwipc_connection* connection, const gwipc_message* commit_message,
                    const gwipc_policy_commit& commit,
                    const glasswyrm::wm::PolicyState& policy,
                    const bool interactive) {
  const auto count = static_cast<std::uint32_t>(
      policy.output_order.size() + (interactive ? 1U : 0U));
  gwipc_snapshot_begin begin{};
  begin.struct_size = sizeof(begin);
  begin.snapshot_id = commit.commit_id;
  begin.domain = GWIPC_SNAPSHOT_WINDOW_POLICY;
  begin.generation = policy.generation;
  begin.expected_item_count = count;
  if (!enqueue_control(connection, GWIPC_MESSAGE_SNAPSHOT_BEGIN, begin,
                       gwipc_control_encode_snapshot_begin))
    return false;
  if (interactive) {
    const auto bindings = default_bindings();
    std::fprintf(stderr,
                 "gwm: interactive bindings move_button=%u resize_button=%u "
                 "close_keysym=0x%04x minimum_width=%u minimum_height=%u\n",
                 static_cast<unsigned>(bindings.move_button),
                 static_cast<unsigned>(bindings.resize_button),
                 static_cast<unsigned>(bindings.close_keysym),
                 static_cast<unsigned>(bindings.minimum_width),
                 static_cast<unsigned>(bindings.minimum_height));
    if (!enqueue_contract(connection, GWIPC_MESSAGE_POLICY_BINDINGS_UPSERT,
                          GWIPC_FLAG_SNAPSHOT_ITEM, 0, bindings,
                          gwipc_contract_encode_policy_bindings_upsert))
      return false;
  }
  for (const auto id : policy.output_order) {
    const auto state = state_from(policy.windows.at(id));
    if (!enqueue_contract(connection, GWIPC_MESSAGE_POLICY_WINDOW_STATE,
                          GWIPC_FLAG_SNAPSHOT_ITEM, 0, state,
                          gwipc_contract_encode_policy_window_state))
      return false;
  }
  gwipc_snapshot_end end{};
  end.struct_size = sizeof(end);
  end.snapshot_id = begin.snapshot_id;
  end.generation = begin.generation;
  end.actual_item_count = count;
  if (!enqueue_control(connection, GWIPC_MESSAGE_SNAPSHOT_END, end,
                       gwipc_control_encode_snapshot_end))
    return false;
  gwipc_policy_acknowledged ack{};
  ack.struct_size = sizeof(ack);
  ack.commit_id = commit.commit_id;
  ack.producer_generation = commit.producer_generation;
  ack.applied_generation = policy.generation;
  ack.policy_hash = policy.hash;
  ack.window_count = static_cast<std::uint32_t>(policy.output_order.size());
  ack.result = GWIPC_POLICY_ACCEPTED;
  return enqueue_contract(connection, GWIPC_MESSAGE_POLICY_ACKNOWLEDGED,
                          GWIPC_FLAG_REPLY,
                          gwipc_message_sequence(commit_message), ack,
                          gwipc_contract_encode_policy_acknowledged);
}

bool enqueue_rejection(gwipc_connection* connection,
                       const gwipc_message* commit_message,
                       const gwipc_policy_commit& commit,
                       const glasswyrm::wm::PolicyState& previous,
                       gwipc_policy_result result) {
  gwipc_policy_acknowledged ack{};
  ack.struct_size = sizeof(ack);
  ack.commit_id = commit.commit_id;
  ack.producer_generation = commit.producer_generation;
  ack.applied_generation = previous.generation;
  ack.policy_hash = previous.hash;
  ack.window_count = static_cast<std::uint32_t>(previous.windows.size());
  ack.result = result;
  return enqueue_contract(connection, GWIPC_MESSAGE_POLICY_ACKNOWLEDGED,
                          GWIPC_FLAG_REPLY,
                          gwipc_message_sequence(commit_message), ack,
                          gwipc_contract_encode_policy_acknowledged);
}

}  // namespace

bool dispatch_control(PeerState& peer, const gwipc_message* message) {
  gwipc_decoded_control* raw = nullptr;
  if (gwipc_control_decode_message(message, &raw) != GWIPC_STATUS_OK)
    return false;
  const std::unique_ptr<gwipc_decoded_control, ControlDeleter> control(raw);
  switch (gwipc_message_type(message)) {
    case GWIPC_MESSAGE_SNAPSHOT_BEGIN: {
      const auto* value = gwipc_decoded_snapshot_begin(control.get());
      if (!value || value->domain != GWIPC_SNAPSHOT_WINDOW_POLICY ||
          !peer.transaction.begin_snapshot())
        return false;
      peer.snapshot_id = value->snapshot_id;
      peer.snapshot_generation = value->generation;
      std::fprintf(stderr, "gwm: snapshot begin id=%llu generation=%llu\n",
                   static_cast<unsigned long long>(value->snapshot_id),
                   static_cast<unsigned long long>(value->generation));
      return true;
    }
    case GWIPC_MESSAGE_SNAPSHOT_END: {
      const auto* value = gwipc_decoded_snapshot_end(control.get());
      const bool valid = value && value->snapshot_id == peer.snapshot_id &&
                         value->generation == peer.snapshot_generation &&
                         peer.transaction.end_snapshot();
      if (valid) {
        peer.snapshot_id = 0;
        peer.snapshot_generation = 0;
      }
      return valid;
    }
    case GWIPC_MESSAGE_SNAPSHOT_ABORT: {
      const auto* value = gwipc_decoded_snapshot_abort(control.get());
      const bool valid = value && value->snapshot_id == peer.snapshot_id &&
                         peer.transaction.abort_snapshot();
      if (valid) {
        peer.snapshot_id = 0;
        peer.snapshot_generation = 0;
      }
      return valid;
    }
    default: return false;
  }
}

bool dispatch_contract(PeerState& peer, gwipc_connection* connection,
                       const gwipc_message* message, bool& accepted) {
  gwipc_decoded_contract* raw = nullptr;
  if (gwipc_contract_decode_message(message, &raw) != GWIPC_STATUS_OK)
    return false;
  const std::unique_ptr<gwipc_decoded_contract, ContractDeleter> contract(raw);
  switch (gwipc_message_type(message)) {
    case GWIPC_MESSAGE_POLICY_CONTEXT_UPSERT: {
      const auto* value = gwipc_decoded_policy_context_upsert(contract.get());
      if (!value || !peer.transaction.upsert(context_from(*value))) return false;
      std::fprintf(stderr,
                   "gwm: context workspace=%u output=%llu work_area=%ux%u+%d+%d\n",
                   value->workspace_id,
                   static_cast<unsigned long long>(value->output_id),
                   value->work_width, value->work_height, value->work_x,
                   value->work_y);
      return true;
    }
    case GWIPC_MESSAGE_POLICY_WINDOW_UPSERT: {
      const auto* value = gwipc_decoded_policy_window_upsert(contract.get());
      if (!value || !peer.transaction.upsert(window_from(*value))) return false;
      std::fprintf(stderr, "gwm: window upsert id=%u map=%u override_redirect=%u\n",
                   value->window_id, static_cast<unsigned>(value->map_intent),
                   static_cast<unsigned>(value->override_redirect));
      return true;
    }
    case GWIPC_MESSAGE_POLICY_LIFECYCLE_WINDOW_UPSERT: {
      const auto* value =
          gwipc_decoded_policy_lifecycle_window_upsert(contract.get());
      if (!value || !peer.transaction.upsert(window_from(*value))) return false;
      std::fprintf(stderr,
                   "gwm: lifecycle window upsert id=%u geometry=%llu "
                   "requested=%ux%u+%d+%d stack=%llu\n",
                   value->window.window_id,
                   static_cast<unsigned long long>(value->geometry_serial),
                   value->window.requested_width,
                   value->window.requested_height,
                   value->window.requested_x,
                   value->window.requested_y,
                   static_cast<unsigned long long>(value->stack_serial));
      return true;
    }
    case GWIPC_MESSAGE_POLICY_OUTPUT_UPSERT: {
      const auto* value = gwipc_decoded_policy_output_upsert(contract.get());
      if (!multi_output_profile(connection) || !value ||
          !peer.transaction.upsert(output_from(*value)))
        return false;
      std::fprintf(stderr,
                   "gwm: output upsert id=%llu logical=%ux%u+%d+%d "
                   "scale=%u/%u primary=%u\n",
                   static_cast<unsigned long long>(value->output_id),
                   value->logical_width, value->logical_height,
                   value->logical_x, value->logical_y,
                   value->scale_numerator, value->scale_denominator,
                   static_cast<unsigned>(value->primary));
      return true;
    }
    case GWIPC_MESSAGE_POLICY_WINDOW_OUTPUT_HINT: {
      const auto* value =
          gwipc_decoded_policy_window_output_hint(contract.get());
      return multi_output_profile(connection) && value &&
             peer.transaction.upsert(hint_from(*value));
    }
    case GWIPC_MESSAGE_POLICY_WINDOW_REMOVE: {
      const auto* value = gwipc_decoded_policy_window_remove(contract.get());
      return value && peer.transaction.remove(value->window_id);
    }
    case GWIPC_MESSAGE_POLICY_COMMIT: {
      const auto* value = gwipc_decoded_policy_commit(contract.get());
      if (!value) return false;
      if (value->commit_id <= peer.last_commit_id ||
          value->producer_generation < peer.last_generation) {
        return enqueue_rejection(connection, message, *value,
                                 peer.transaction.committed_policy(),
                                 GWIPC_POLICY_REJECTED_INVALID_WINDOW);
      }
      peer.last_commit_id = value->commit_id;
      peer.last_generation = value->producer_generation;
      const bool interactive =
          (gwipc_connection_peer_info(connection).capabilities &
           GWIPC_CAP_INTERACTIVE_POLICY) != 0;
      if (multi_output_profile(connection) &&
          peer.transaction.pending().outputs.empty()) {
        return enqueue_rejection(connection, message, *value,
                                 peer.transaction.committed_policy(),
                                 GWIPC_POLICY_REJECTED_INCOMPLETE_SNAPSHOT);
      }
      auto evaluation = peer.transaction.commit(
          value->producer_generation,
          interactive ? preflight_interactive_policy : preflight_policy);
      if (!evaluation) {
        if (evaluation.error == glasswyrm::wm::EvaluationError::OutputFailure)
          return false;
        const auto result = result_from(evaluation.error);
        if (!enqueue_rejection(connection, message, *value,
                               peer.transaction.committed_policy(), result))
          return false;
        std::fprintf(stderr, "gwm: policy rejected commit=%llu result=%u\n",
                     static_cast<unsigned long long>(value->commit_id),
                     static_cast<unsigned>(result));
        return true;
      }
      if (interactive) {
        const glasswyrm::wm::InteractiveBindings bindings;
        evaluation.policy.hash = glasswyrm::wm::interactive_policy_hash(
            evaluation.policy, bindings);
        peer.transaction.set_committed_policy_hash(evaluation.policy.hash);
      }
      if (!enqueue_policy(connection, message, *value, evaluation.policy,
                          interactive))
        return false;
      accepted = true;
      std::fprintf(stderr,
                   "gwm: policy accepted commit=%llu generation=%llu windows=%zu hash=%016llx\n",
                   static_cast<unsigned long long>(value->commit_id),
                   static_cast<unsigned long long>(evaluation.policy.generation),
                   evaluation.policy.windows.size(),
                   static_cast<unsigned long long>(evaluation.policy.hash));
      return true;
    }
    default: return false;
  }
}

void PeerState::disconnect() noexcept { *this = {}; }

}  // namespace glasswyrm::wm::runtime
