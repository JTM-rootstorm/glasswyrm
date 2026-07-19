#include "glasswyrmd/policy_peer.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string_view>
#include <type_traits>

namespace glasswyrm::server {
namespace {
constexpr gwipc_capabilities kCapabilities =
    GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_WINDOW_POLICY | GWIPC_CAP_WINDOW_LIFECYCLE |
    GWIPC_CAP_INTERACTIVE_POLICY;
constexpr gwipc_capabilities kOutputModelCapabilities =
    GWIPC_CAP_MULTI_OUTPUT_POLICY | GWIPC_CAP_SCALE_METADATA;
struct ContractDelete {
  void operator()(gwipc_contract_payload *p) const {
    gwipc_contract_payload_destroy(p);
  }
};
struct ControlDelete {
  void operator()(gwipc_control_payload *p) const {
    gwipc_control_payload_destroy(p);
  }
};
struct DecodedContractDelete {
  void operator()(gwipc_decoded_contract *p) const {
    gwipc_decoded_contract_destroy(p);
  }
};
struct DecodedControlDelete {
  void operator()(gwipc_decoded_control *p) const {
    gwipc_decoded_control_destroy(p);
  }
};
void hash_byte(std::uint64_t &hash, std::uint8_t value) {
  hash = (hash ^ value) * UINT64_C(1099511628211);
}
template <class T> void hash_little(std::uint64_t &hash, T value) {
  using U = std::make_unsigned_t<T>;
  auto bits = static_cast<std::uint64_t>(static_cast<U>(value));
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    hash_byte(hash, static_cast<std::uint8_t>(bits));
    bits >>= 8U;
  }
}

gwipc_policy_context_upsert
policy_context(const PolicySnapshotSubmission &submission,
               const gw::protocol::x11::ScreenModel &screen) {
  gwipc_policy_context_upsert context{};
  context.struct_size = sizeof(context);
  context.root_window_id = screen.root_window;
  context.workspace_id = 1;
  context.output_id = 1;
  context.work_width = screen.width_pixels;
  context.work_height = screen.height_pixels;
  if (submission.outputs.empty())
    return context;
  std::int64_t maximum_x = 0;
  std::int64_t maximum_y = 0;
  for (const auto &output : submission.outputs) {
    if (output.enabled) {
      maximum_x = std::max(
          maximum_x, static_cast<std::int64_t>(output.logical_x) +
                         output.logical_width);
      maximum_y = std::max(
          maximum_y, static_cast<std::int64_t>(output.logical_y) +
                         output.logical_height);
    }
    if (output.enabled && output.primary)
      context.output_id = output.output_id;
  }
  context.work_width = static_cast<std::uint32_t>(maximum_x);
  context.work_height = static_cast<std::uint32_t>(maximum_y);
  return context;
}

std::uint64_t
canonical_policy_hash(const PolicySnapshotResult &result,
                      const PolicySnapshotSubmission &submission,
                      const gw::protocol::x11::ScreenModel &screen) {
  std::uint64_t hash = UINT64_C(14695981039346656037);
  const bool output_model = !submission.outputs.empty();
  for (char byte : output_model ? std::string_view("glasswyrm-policy-v3")
                                : std::string_view("glasswyrm-policy-v1"))
    hash_byte(hash, static_cast<std::uint8_t>(byte));
  const auto context = policy_context(submission, screen);
  hash_little(hash, result.generation);
  hash_little(hash, context.root_window_id);
  hash_little(hash, context.workspace_id);
  hash_little(hash, context.output_id);
  hash_little(hash, context.work_x);
  hash_little(hash, context.work_y);
  hash_little(hash, context.work_width);
  hash_little(hash, context.work_height);
  hash_little(hash, context.flags);
  if (output_model) {
    std::map<std::uint64_t, const gwipc_policy_output_upsert *> outputs;
    for (const auto &output : submission.outputs)
      outputs.emplace(output.output_id, &output);
    hash_little(hash, static_cast<std::uint32_t>(outputs.size()));
    for (const auto &[id, output] : outputs) {
      (void)id;
      hash_little(hash, output->output_id);
      hash_little(hash, output->logical_x);
      hash_little(hash, output->logical_y);
      hash_little(hash, output->logical_width);
      hash_little(hash, output->logical_height);
      hash_little(hash, output->work_x);
      hash_little(hash, output->work_y);
      hash_little(hash, output->work_width);
      hash_little(hash, output->work_height);
      hash_little(hash, output->scale_numerator);
      hash_little(hash, output->scale_denominator);
      hash_little(hash, static_cast<std::uint8_t>(output->transform));
      hash_little(hash, output->enabled);
      hash_little(hash, output->primary);
      hash_little(hash, output->flags);
    }
    std::map<std::uint32_t, const gwipc_policy_window_output_hint *> hints;
    for (const auto &hint : submission.output_hints)
      hints.emplace(hint.window_id, &hint);
    hash_little(hash, static_cast<std::uint32_t>(hints.size()));
    for (const auto &[id, hint] : hints) {
      (void)id;
      hash_little(hash, hint->window_id);
      hash_little(hash, hint->previous_output_id);
      hash_little(hash, hint->preferred_output_id);
      hash_little(hash, hint->flags);
    }
  }
  for (const auto &s : result.windows) {
    hash_little(hash, s.window_id);
    hash_little(hash, s.transient_for);
    hash_little(hash, s.workspace_id);
    hash_little(hash, UINT32_C(0));
    hash_little(hash, s.output_id);
    hash_little(hash, s.final_x);
    hash_little(hash, s.final_y);
    hash_little(hash, s.final_width);
    hash_little(hash, s.final_height);
    hash_little(hash, s.stacking);
    hash_little(hash, static_cast<std::uint16_t>(s.window_type));
    hash_little(hash, static_cast<std::uint16_t>(s.applied_state));
    hash_byte(hash, s.visible);
    hash_byte(hash, s.focused);
    hash_byte(hash, s.managed);
    hash_byte(hash, s.decoration_eligible);
    hash_byte(hash, s.override_redirect);
    hash_byte(hash, s.attention_requested);
    hash_byte(hash, static_cast<std::uint8_t>(s.fullscreen_eligible));
    hash_byte(hash, static_cast<std::uint8_t>(s.direct_scanout_eligible));
    hash_little(hash, UINT32_C(0));
    hash_little(hash, UINT32_C(0));
  }
  if (result.bindings) {
    const auto v1 = hash;
    hash = UINT64_C(14695981039346656037);
    for (char byte : output_model ? std::string_view("glasswyrm-policy-v3")
                                  : std::string_view("glasswyrm-policy-v2"))
      hash_byte(hash, static_cast<std::uint8_t>(byte));
    hash_little(hash, v1);
    const auto &bindings = *result.bindings;
    hash_little(hash, bindings.move_modifiers);
    hash_little(hash, bindings.resize_modifiers);
    hash_little(hash, bindings.close_modifiers);
    hash_little(hash, bindings.move_button);
    hash_little(hash, bindings.resize_button);
    hash_little(hash, bindings.close_keysym);
    hash_little(hash, bindings.minimum_width);
    hash_little(hash, bindings.minimum_height);
    hash_byte(hash, bindings.raise_on_focus);
    hash_byte(hash, bindings.consume_wm_bindings);
  }
  return hash;
}
bool valid_policy_result(const PolicySnapshotSubmission &input,
                         const PolicySnapshotResult &result,
                         const bool interactive, const bool output_model) {
  if (result.bindings.has_value() != interactive)
    return false;
  std::set<std::uint32_t> expected;
  for (const auto &window : input.windows)
    if (!expected.insert(window.window.window_id).second)
      return false;
  std::set<std::uint32_t> actual;
  std::set<std::int32_t> stacks;
  std::set<std::uint64_t> enabled_outputs;
  for (const auto &output : input.outputs)
    if (output.enabled)
      enabled_outputs.insert(output.output_id);
  unsigned focused = 0;
  for (const auto &state : result.windows) {
    if (!actual.insert(state.window_id).second ||
        !expected.contains(state.window_id) || state.workspace_id != 1 ||
        (output_model ? !enabled_outputs.contains(state.output_id)
                      : state.output_id != 1) ||
        state.final_width == 0 ||
        state.final_height == 0 ||
        (state.visible &&
         (state.stacking < 0 || !stacks.insert(state.stacking).second)) ||
        (!state.visible && state.stacking != -1) ||
        (state.focused && (!state.visible || ++focused > 1)))
      return false;
  }
  if (actual != expected)
    return false;
  for (std::size_t index = 0; index < stacks.size(); ++index)
    if (!stacks.contains(static_cast<std::int32_t>(index)))
      return false;
  return true;
}

bool valid_submission_profile(const PolicySnapshotSubmission &submission,
                              const bool output_model) {
  if (!output_model)
    return submission.outputs.empty() && submission.output_hints.empty();
  if (submission.outputs.empty() || submission.outputs.size() > 8 ||
      submission.output_hints.size() > submission.windows.size())
    return false;
  std::set<std::uint64_t> output_ids;
  std::set<std::uint32_t> window_ids;
  std::set<std::uint32_t> hint_ids;
  unsigned primary_count = 0;
  for (const auto &window : submission.windows)
    window_ids.insert(window.window.window_id);
  for (const auto &output : submission.outputs) {
    if (output.output_id == 0 || !output_ids.insert(output.output_id).second)
      return false;
    primary_count += output.enabled && output.primary;
  }
  if (primary_count != 1)
    return false;
  for (const auto &hint : submission.output_hints) {
    if (hint.window_id == 0 || !hint_ids.insert(hint.window_id).second ||
        !window_ids.contains(hint.window_id) ||
        (hint.previous_output_id != 0 &&
         !output_ids.contains(hint.previous_output_id)) ||
        (hint.preferred_output_id != 0 &&
         !output_ids.contains(hint.preferred_output_id)))
      return false;
  }
  return true;
}

template <class T, class Encoder>
bool enqueue_contract(gwipc_connection *connection, std::uint16_t type,
                      std::uint32_t flags, const T &value, Encoder encoder,
                      std::uint64_t *out_sequence = nullptr) {
  gwipc_contract_payload *raw = nullptr;
  if (encoder(&value, &raw) != GWIPC_STATUS_OK)
    return false;
  std::unique_ptr<gwipc_contract_payload, ContractDelete> payload(raw);
  std::size_t size = 0;
  const auto *data = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.flags = flags;
  message.payload = data;
  message.payload_size = size;
  if (out_sequence)
    return gwipc_connection_enqueue_with_sequence(
               connection, &message, out_sequence) == GWIPC_STATUS_OK;
  return gwipc_connection_enqueue(connection, &message) == GWIPC_STATUS_OK;
}
template <class T, class Encoder>
bool enqueue_control(gwipc_connection *connection, std::uint16_t type,
                     const T &value, Encoder encoder) {
  gwipc_control_payload *raw = nullptr;
  if (encoder(&value, &raw) != GWIPC_STATUS_OK)
    return false;
  std::unique_ptr<gwipc_control_payload, ControlDelete> payload(raw);
  std::size_t size = 0;
  const auto *data = gwipc_control_payload_data(payload.get(), &size);
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.payload = data;
  message.payload_size = size;
  return gwipc_connection_enqueue(connection, &message) == GWIPC_STATUS_OK;
}
} // namespace

PolicyPeer::PolicyPeer(std::string path,
                       const gw::protocol::x11::ScreenModel screen,
                       const bool interactive_policy,
                       const bool output_model)
    : transport_(std::move(path), GWIPC_ROLE_WINDOW_MANAGER,
                 (interactive_policy
                      ? kCapabilities
                      : kCapabilities & ~GWIPC_CAP_INTERACTIVE_POLICY) |
                     (output_model ? kOutputModelCapabilities : 0),
                 "glasswyrmd-policy"),
      screen_(screen), output_model_profile_(output_model) {}

bool PolicyPeer::connect(std::string &error) {
  disconnect();
  if (!transport_.connect(error))
    return false;
  state_ = PeerBootstrapState::Connecting;
  return true;
}

bool PolicyPeer::send_bootstrap(std::string &error) {
  interactive_profile_ =
      (gwipc_connection_peer_info(transport_.connection()).capabilities &
       GWIPC_CAP_INTERACTIVE_POLICY) != 0;
  if (output_model_profile_ && replay_input_.commit_id == 0)
    return true;
  const bool replay = replay_input_.commit_id != 0;
  if (!submit(replay ? replay_input_
                     : PolicySnapshotSubmission{1, 1, {}, {}, {}},
              error))
    return false;
  replaying_ = replay;
  return true;
}

bool PolicyPeer::submit(const PolicySnapshotSubmission &submission,
                        std::string &error) {
  if (!transport_.established() ||
      (state_ != PeerBootstrapState::Connecting &&
       state_ != PeerBootstrapState::Synchronized) ||
      submission.commit_id == 0 || submission.generation == 0) {
    error = "policy peer is not ready for a snapshot";
    return false;
  }
  if (!valid_submission_profile(submission, output_model_profile_)) {
    error = "policy snapshot does not match the negotiated output profile";
    return false;
  }
  auto *connection = transport_.connection();
  const auto count = static_cast<std::uint32_t>(
      submission.windows.size() + submission.outputs.size() +
      submission.output_hints.size() + 1);
  gwipc_snapshot_begin begin{sizeof(begin),
                             submission.commit_id,
                             GWIPC_SNAPSHOT_WINDOW_POLICY,
                             0,
                             submission.generation,
                             count,
                             {}};
  const auto context = policy_context(submission, screen_);
  gwipc_snapshot_end end{
      sizeof(end), submission.commit_id, submission.generation, count, {}};
  gwipc_policy_commit commit{};
  commit.struct_size = sizeof(commit);
  commit.commit_id = submission.commit_id;
  commit.producer_generation = submission.generation;
  if (!enqueue_control(connection, GWIPC_MESSAGE_SNAPSHOT_BEGIN, begin,
                       gwipc_control_encode_snapshot_begin) ||
      !enqueue_contract(connection, GWIPC_MESSAGE_POLICY_CONTEXT_UPSERT,
                        GWIPC_FLAG_SNAPSHOT_ITEM, context,
                        gwipc_contract_encode_policy_context_upsert)) {
    error = "could not queue policy snapshot header";
    return false;
  }
  for (const auto &output : submission.outputs) {
    if (!enqueue_contract(connection, GWIPC_MESSAGE_POLICY_OUTPUT_UPSERT,
                          GWIPC_FLAG_SNAPSHOT_ITEM, output,
                          gwipc_contract_encode_policy_output_upsert)) {
      error = "could not queue policy output";
      return false;
    }
  }
  for (const auto &window : submission.windows) {
    if (!enqueue_contract(
            connection, GWIPC_MESSAGE_POLICY_LIFECYCLE_WINDOW_UPSERT,
            GWIPC_FLAG_SNAPSHOT_ITEM, window,
            gwipc_contract_encode_policy_lifecycle_window_upsert)) {
      error = "could not queue lifecycle policy window";
      return false;
    }
  }
  for (const auto &hint : submission.output_hints) {
    if (!enqueue_contract(connection,
                          GWIPC_MESSAGE_POLICY_WINDOW_OUTPUT_HINT,
                          GWIPC_FLAG_SNAPSHOT_ITEM, hint,
                          gwipc_contract_encode_policy_window_output_hint)) {
      error = "could not queue policy window output hint";
      return false;
    }
  }
  if (!enqueue_control(connection, GWIPC_MESSAGE_SNAPSHOT_END, end,
                       gwipc_control_encode_snapshot_end) ||
      !enqueue_contract(
          connection, GWIPC_MESSAGE_POLICY_COMMIT, GWIPC_FLAG_ACK_REQUIRED,
          commit, gwipc_contract_encode_policy_commit, &commit_sequence_)) {
    error = "could not queue policy bootstrap";
    return false;
  }
  pending_ = submission;
  result_ = {};
  reply_snapshot_active_ = false;
  reply_snapshot_complete_ = false;
  replaying_ = false;
  state_ = PeerBootstrapState::AwaitingReply;
  return true;
}

PeerProcessOutcome PolicyPeer::drain(std::string &error) {
  for (;;) {
    glasswyrm::ipc::Message message;
    const auto status = transport_.handle().receive(message);
    if (status == GWIPC_STATUS_WOULD_BLOCK)
      return PeerProcessOutcome::Progress;
    if (status != GWIPC_STATUS_OK) {
      error = "policy peer receive failed";
      return PeerProcessOutcome::Fatal;
    }
    const auto type = gwipc_message_type(message.get());
    if (type == GWIPC_MESSAGE_SNAPSHOT_BEGIN ||
        type == GWIPC_MESSAGE_SNAPSHOT_END) {
      gwipc_decoded_control *raw = nullptr;
      if (gwipc_control_decode_message(message.get(), &raw) != GWIPC_STATUS_OK)
        return PeerProcessOutcome::Fatal;
      std::unique_ptr<gwipc_decoded_control, DecodedControlDelete> decoded(raw);
      if (type == GWIPC_MESSAGE_SNAPSHOT_BEGIN) {
        const auto *value = gwipc_decoded_snapshot_begin(decoded.get());
        if (!value || reply_snapshot_active_ ||
            value->snapshot_id != pending_.commit_id ||
            value->domain != GWIPC_SNAPSHOT_WINDOW_POLICY ||
            value->generation != pending_.generation ||
            value->expected_item_count !=
                pending_.windows.size() + (interactive_profile_ ? 1U : 0U)) {
          error = "invalid policy bootstrap snapshot";
          return PeerProcessOutcome::Fatal;
        }
        reply_snapshot_active_ = true;
      } else {
        const auto *value = gwipc_decoded_snapshot_end(decoded.get());
        if (!value || !reply_snapshot_active_ ||
            value->snapshot_id != pending_.commit_id ||
            value->generation != pending_.generation ||
            value->actual_item_count !=
                pending_.windows.size() + (interactive_profile_ ? 1U : 0U) ||
            result_.windows.size() != pending_.windows.size() ||
            result_.bindings.has_value() != interactive_profile_) {
          error = "invalid policy bootstrap snapshot end";
          return PeerProcessOutcome::Fatal;
        }
        reply_snapshot_active_ = false;
        reply_snapshot_complete_ = true;
      }
      continue;
    }
    if (type == GWIPC_MESSAGE_POLICY_WINDOW_STATE) {
      gwipc_decoded_contract *raw = nullptr;
      if (gwipc_contract_decode_message(message.get(), &raw) != GWIPC_STATUS_OK)
        return PeerProcessOutcome::Fatal;
      std::unique_ptr<gwipc_decoded_contract, DecodedContractDelete> decoded(
          raw);
      const auto *value = gwipc_decoded_policy_window_state(decoded.get());
      if (!value || !reply_snapshot_active_)
        return PeerProcessOutcome::Fatal;
      result_.windows.push_back(*value);
      continue;
    }
    if (type == GWIPC_MESSAGE_POLICY_BINDINGS_UPSERT) {
      gwipc_decoded_contract *raw = nullptr;
      if (gwipc_contract_decode_message(message.get(), &raw) != GWIPC_STATUS_OK)
        return PeerProcessOutcome::Fatal;
      std::unique_ptr<gwipc_decoded_contract, DecodedContractDelete> decoded(
          raw);
      const auto *value = gwipc_decoded_policy_bindings_upsert(decoded.get());
      if (!interactive_profile_ || !value || !reply_snapshot_active_ ||
          result_.bindings) {
        error = "invalid or duplicate interactive policy bindings";
        return PeerProcessOutcome::Fatal;
      }
      result_.bindings = *value;
      continue;
    }
    if (type != GWIPC_MESSAGE_POLICY_ACKNOWLEDGED) {
      error = "unexpected policy bootstrap message";
      return PeerProcessOutcome::Fatal;
    }
    gwipc_decoded_contract *raw = nullptr;
    if (gwipc_contract_decode_message(message.get(), &raw) != GWIPC_STATUS_OK)
      return PeerProcessOutcome::Fatal;
    std::unique_ptr<gwipc_decoded_contract, DecodedContractDelete> decoded(raw);
    const auto *ack = gwipc_decoded_policy_acknowledged(decoded.get());
    if (ack && ack->commit_id == pending_.commit_id &&
        ack->producer_generation == pending_.generation &&
        ack->result >= GWIPC_POLICY_REJECTED_INCOMPLETE_SNAPSHOT &&
        ack->result <= GWIPC_POLICY_REJECTED_UNSUPPORTED_METADATA &&
        (gwipc_message_flags(message.get()) & GWIPC_FLAG_REPLY) != 0 &&
        gwipc_message_reply_to(message.get()) == commit_sequence_) {
      state_ = PeerBootstrapState::Synchronized;
      return PeerProcessOutcome::SemanticRejected;
    }
    if (!ack || !reply_snapshot_complete_ ||
        ack->commit_id != pending_.commit_id ||
        ack->producer_generation != pending_.generation ||
        ack->applied_generation != pending_.generation ||
        ack->window_count != pending_.windows.size() ||
        ack->result != GWIPC_POLICY_ACCEPTED ||
        (gwipc_message_flags(message.get()) & GWIPC_FLAG_REPLY) == 0 ||
        gwipc_message_reply_to(message.get()) != commit_sequence_) {
      error = "invalid policy bootstrap acknowledgement";
      return PeerProcessOutcome::Fatal;
    }
    result_.generation = ack->applied_generation;
    result_.hash = ack->policy_hash;
    if (!valid_policy_result(pending_, result_, interactive_profile_,
                             output_model_profile_) ||
        canonical_policy_hash(result_, pending_, screen_) != ack->policy_hash ||
        (replaying_ && policy_hash_ != 0 && policy_hash_ != ack->policy_hash)) {
      error = "policy acknowledgement hash does not match returned snapshot";
      return PeerProcessOutcome::Fatal;
    }
    policy_hash_ = ack->policy_hash;
    replay_input_ = pending_;
    state_ = PeerBootstrapState::Synchronized;
  }
}

PeerProcessOutcome PolicyPeer::process(const short revents, std::string &error) {
  if (!transport_.process(revents, error)) {
    state_ = PeerBootstrapState::Failed;
    return PeerProcessOutcome::Disconnected;
  }
  if (state_ == PeerBootstrapState::Connecting && transport_.established() &&
      !send_bootstrap(error)) {
    state_ = PeerBootstrapState::Failed;
    return PeerProcessOutcome::Fatal;
  }
  if (state_ == PeerBootstrapState::AwaitingReply) {
    const auto outcome = drain(error);
    if (outcome == PeerProcessOutcome::Fatal)
      state_ = PeerBootstrapState::Failed;
    return outcome;
  }
  return PeerProcessOutcome::Progress;
}

void PolicyPeer::disconnect() noexcept {
  transport_.disconnect();
  state_ = PeerBootstrapState::Disconnected;
  commit_sequence_ = 0;
  reply_snapshot_active_ = false;
  reply_snapshot_complete_ = false;
  interactive_profile_ = false;
  replaying_ = false;
}
} // namespace glasswyrm::server
