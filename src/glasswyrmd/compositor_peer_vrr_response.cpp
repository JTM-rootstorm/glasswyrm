#include "glasswyrmd/compositor_peer.hpp"

#include "glasswyrmd/compositor_buffer_replay.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace glasswyrm::server {

PeerProcessOutcome CompositorPeer::finish_vrr_response(std::string& error) {
  if (!frame_acknowledged_ || !vrr_response_.acknowledgement) {
    error = "M14 compositor response completed without a frame acknowledgement";
    return PeerProcessOutcome::Fatal;
  }
  const auto& scene = content_submission_ ? replay_input_ : pending_;
  const auto commit_id = content_submission_ ? pending_content_.commit_id
                                             : pending_.commit_id;
  const auto generation = content_submission_ ? pending_content_.generation
                                              : pending_.generation;
  std::map<std::uint64_t, gwipc_vrr_policy_mode> expected_outputs;
  for (const auto& output : scene.outputs) {
    if (!output.enabled) continue;
    const auto policy = std::ranges::find_if(
        scene.output_vrr_policies, [&](const auto& value) {
          return value.output_id == output.output_id;
        });
    if (policy == scene.output_vrr_policies.end() ||
        !expected_outputs.emplace(output.output_id, policy->mode).second) {
      error = "M14 compositor response has no policy for a presented output";
      return PeerProcessOutcome::Fatal;
    }
  }
  std::set<std::uint64_t> states;
  for (const auto& state : vrr_response_.output_states) {
    const auto expected = expected_outputs.find(state.output_id);
    if (expected == expected_outputs.end() ||
        !states.insert(state.output_id).second ||
        state.requested_mode != expected->second ||
        state.last_commit_id != commit_id ||
        state.last_presented_generation != generation) {
      error = "M14 compositor returned an invalid VRR output state batch";
      return PeerProcessOutcome::Fatal;
    }
  }
  std::set<std::uint64_t> timings;
  for (const auto& timing : vrr_response_.timings)
    if (!expected_outputs.contains(timing.output_id) ||
        !timings.insert(timing.output_id).second ||
        timing.commit_id != commit_id ||
        timing.presented_generation != generation) {
      error = "M14 compositor returned an invalid presentation timing batch";
      return PeerProcessOutcome::Fatal;
    }
  std::set<std::uint64_t> expected_ids;
  for (const auto& [id, mode] : expected_outputs) {
    static_cast<void>(mode);
    expected_ids.insert(id);
  }
  const auto* expectation = vrr_cache_.expectation();
  if (!expectation) {
    error = "M14 compositor response has no staged expectation";
    return PeerProcessOutcome::Fatal;
  }
  const std::set<std::uint64_t> actual_releases(
      vrr_response_.released_buffer_ids.begin(),
      vrr_response_.released_buffer_ids.end());
  if (states != expected_ids || timings != expected_ids ||
      actual_releases.size() != vrr_response_.released_buffer_ids.size() ||
      actual_releases != expectation->release_buffer_ids) {
    error = "M14 compositor response batch is incomplete";
    return PeerProcessOutcome::Fatal;
  }
  if (vrr_cache_.promote(vrr_response_) != VrrResponseStatus::Accepted) {
    error = "M14 compositor response failed server promotion preflight";
    return PeerProcessOutcome::Fatal;
  }
  accepted_vrr_cache_ = vrr_cache_;
  if (content_submission_)
    compositor_buffer_replay::promote_content(pending_content_, replay_input_);
  else
    compositor_buffer_replay::promote(pending_, replay_input_);
  content_submission_ = false;
  frame_acknowledged_ = false;
  state_ = PeerBootstrapState::Synchronized;
  if (reconnect_staged_vrr_cache_) {
    vrr_cache_ = std::move(*reconnect_staged_vrr_cache_);
    reconnect_staged_vrr_cache_.reset();
  }
  error.clear();
  return PeerProcessOutcome::Progress;
}

} // namespace glasswyrm::server
