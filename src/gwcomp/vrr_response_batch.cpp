#include "gwcomp/vrr_response_batch.hpp"

#include <memory>

namespace gw::compositor {
namespace {

struct PayloadDeleter {
  void operator()(gwipc_contract_payload* value) const {
    gwipc_contract_payload_destroy(value);
  }
};

template <typename Record, typename Encoder>
bool append(std::vector<VrrResponseMessage>& messages, const std::uint16_t type,
            const std::uint16_t flags, const Record& record, Encoder encoder,
            std::string& error) {
  gwipc_contract_payload* raw = nullptr;
  if (encoder(&record, &raw) != GWIPC_STATUS_OK) {
    error = "could not encode preflighted VRR response record";
    return false;
  }
  std::unique_ptr<gwipc_contract_payload, PayloadDeleter> payload(raw);
  std::size_t size = 0;
  const auto* bytes = gwipc_contract_payload_data(payload.get(), &size);
  VrrResponseMessage message{type, flags, {}};
  message.payload.assign(bytes, bytes + size);
  messages.push_back(std::move(message));
  return true;
}

bool encode_messages(
    const CommittedVrrState::OutputStateMap& states,
    const CommittedVrrState::TimingMap& timings,
    const gwipc_frame_commit& commit, const gwipc_frame_result result,
    const VrrResponseBatch::ReleaseMap& releases,
    std::vector<VrrResponseMessage>& messages, std::string& error) {
  messages.clear();
  messages.reserve(states.size() * 2U + releases.size() + 1U);
  for (const auto& [output_id, state] : states) {
    if (!timings.contains(output_id) ||
        !append(messages, GWIPC_MESSAGE_OUTPUT_VRR_STATE_UPSERT,
                GWIPC_FLAG_REPLY, state,
                gwipc_contract_encode_output_vrr_state_upsert, error))
      return false;
  }
  // The response contract is grouped by record kind, not interleaved by
  // output: all effective states, then all timing records, then the ack and
  // releases.  Map iteration keeps each group ordered by stable output ID.
  for (const auto& [output_id, timing] : timings) {
    if (!states.contains(output_id) ||
        !append(messages, GWIPC_MESSAGE_PRESENTATION_TIMING, 0, timing,
                gwipc_contract_encode_presentation_timing, error))
      return false;
  }
  gwipc_frame_acknowledged acknowledged{};
  acknowledged.struct_size = sizeof(acknowledged);
  acknowledged.commit_id = commit.commit_id;
  acknowledged.output_id = commit.output_id;
  acknowledged.presented_generation = commit.producer_generation;
  acknowledged.result = result;
  if (!append(messages, GWIPC_MESSAGE_FRAME_ACKNOWLEDGED, GWIPC_FLAG_REPLY,
              acknowledged, gwipc_contract_encode_frame_acknowledged, error))
    return false;
  for (const auto& [buffer_id, reason] : releases) {
    gwipc_buffer_release release{};
    release.struct_size = sizeof(release);
    release.buffer_id = buffer_id;
    release.reason = reason;
    if (!append(messages, GWIPC_MESSAGE_BUFFER_RELEASE, 0, release,
                gwipc_contract_encode_buffer_release, error))
      return false;
  }
  error.clear();
  return true;
}

} // namespace

std::optional<VrrResponseBatch> VrrResponseBatch::preflight(
    const PreparedVrrFrame& prepared, const gwipc_frame_commit& commit,
    const gwipc_frame_result result, const ReleaseMap& releases,
    std::string& error) {
  if (prepared.requests.empty() || commit.commit_id == 0 ||
      commit.producer_generation == 0) {
    error = "VRR response preflight is missing frame state";
    return std::nullopt;
  }
  CommittedVrrState::OutputStateMap placeholder_states;
  CommittedVrrState::TimingMap placeholder_timings;
  for (const auto& [output_id, request] : prepared.requests) {
    gwipc_output_vrr_state_upsert state{};
    state.struct_size = sizeof(state);
    state.output_id = output_id;
    state.requested_mode =
        static_cast<gwipc_vrr_policy_mode>(request.requested_mode);
    state.decision = static_cast<gwipc_vrr_decision>(request.decision);
    state.desired_enabled = request.desired_enabled;
    state.session_active = 1;
    state.candidate_window_id = request.candidate_window_id;
    state.candidate_surface_id = request.candidate_surface_id;
    state.reason_flags = request.reason_flags;
    state.state_generation = request.state_generation;
    state.transition_serial = request.transition_serial;
    state.last_commit_id = commit.commit_id;
    state.last_presented_generation = commit.producer_generation;
    placeholder_states.emplace(output_id, state);

    gwipc_presentation_timing timing{};
    timing.struct_size = sizeof(timing);
    timing.output_id = output_id;
    timing.commit_id = commit.commit_id;
    timing.presented_generation = commit.producer_generation;
    placeholder_timings.emplace(output_id, timing);
  }
  VrrResponseBatch batch;
  batch.commit_ = commit;
  batch.result_ = result;
  batch.releases_ = releases;
  if (!encode_messages(placeholder_states, placeholder_timings, commit, result,
                       releases, batch.messages_, error))
    return std::nullopt;
  batch.reserved_messages_ = batch.messages_.size();
  for (const auto& message : batch.messages_)
    batch.reserved_payload_bytes_ += message.payload.size();
  batch.messages_.clear();
  return batch;
}

bool VrrResponseBatch::finalize(const CompletedVrrFrame& completed,
                                std::string& error) {
  std::vector<VrrResponseMessage> messages;
  if (!encode_messages(completed.states, completed.timings, commit_, result_,
                       releases_, messages, error))
    return false;
  std::size_t bytes = 0;
  for (const auto& message : messages)
    bytes += message.payload.size();
  if (messages.size() != reserved_messages_ ||
      bytes != reserved_payload_bytes_) {
    error = "final VRR response exceeded its preflight reservation";
    return false;
  }
  messages_ = std::move(messages);
  ready_ = true;
  error.clear();
  return true;
}

} // namespace gw::compositor
