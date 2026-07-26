#include "glasswyrmd/vrr_events.hpp"

#include "glasswyrmd/client_connection.hpp"

#include <algorithm>
#include <optional>

namespace glasswyrm::server {
namespace {

OutputVrrPolicyMode output_policy(const gwipc_vrr_policy_mode mode) noexcept {
  return static_cast<OutputVrrPolicyMode>(mode);
}

WindowVrrPreference preference(
    const gwipc_vrr_window_preference value) noexcept {
  return static_cast<WindowVrrPreference>(value);
}

}  // namespace

VrrSessionStateStatus synchronize_vrr_session_state(
    VrrStateCache& cache, VrrWindowStateStore& published,
    const std::map<std::uint64_t, std::uint32_t>& output_xids,
    const gwipc_session_state state, VrrEventBatch& events) {
  auto staged_cache = cache;
  const auto status = staged_cache.apply_session_state(state);
  if (status != VrrSessionStateStatus::Applied) return status;

  VrrEventBatch staged_events;
  std::optional<VrrWindowStateStore> staged_published;
  if (state == GWIPC_SESSION_INACTIVE) {
    staged_events =
        prepare_vrr_event_batch(staged_cache, published, output_xids);
    if (staged_events.outputs.size() != staged_cache.outputs().size() ||
        staged_events.windows.size() != staged_cache.windows().size())
      return VrrSessionStateStatus::OutputStateIncoherent;
    staged_published = published;
    apply_vrr_event_batch(*staged_published, staged_events);
  }

  cache = std::move(staged_cache);
  if (staged_published) published = std::move(*staged_published);
  events = std::move(staged_events);
  return VrrSessionStateStatus::Applied;
}

VrrEventBatch prepare_vrr_event_batch(
    const VrrStateCache& cache, const VrrWindowStateStore& published,
    const std::map<std::uint64_t, std::uint32_t>& output_xids) {
  VrrEventBatch batch;
  for (const auto& [output_id, value] : cache.outputs()) {
    const auto xid = output_xids.find(output_id);
    if (xid == output_xids.end()) return {};
    PublishedOutputVrrState output;
    output.policy = output_policy(value.policy.mode);
    output.connector_property_present =
        value.capability.connector_property_present != 0;
    output.hardware_capable = value.capability.hardware_capable != 0;
    output.kms_controllable = value.capability.kms_controllable != 0;
    output.simulated = value.capability.simulated != 0;
    output.range_available = value.capability.range_available != 0;
    output.minimum_refresh_millihertz =
        value.capability.minimum_refresh_millihertz;
    output.maximum_refresh_millihertz =
        value.capability.maximum_refresh_millihertz;
    if (value.compositor_state) {
      output.effective_enabled =
          value.compositor_state->effective_enabled != 0;
      output.candidate_window =
          value.compositor_state->candidate_window_id;
      output.reason_flags = value.compositor_state->reason_flags;
      output.state_generation = value.compositor_state->state_generation;
    }
    if (value.timing)
      output.latest_interval_nanoseconds = value.timing->interval_nanoseconds;
    batch.outputs.emplace(xid->second, output);
  }
  batch.windows.reserve(cache.windows().size());
  for (const auto& [window_id, value] : cache.windows()) {
    WindowVrrState before;
    if (const auto* existing = published.find_window(window_id))
      before = *existing;
    WindowVrrState after = before;
    after.preference = preference(value.preference);
    OutputVrrPolicyMode mode = OutputVrrPolicyMode::Off;
    const auto output_id = value.policy_result
                               ? value.policy_result->output_id
                               : value.compositor_state
                                     ? value.compositor_state->output_id
                                     : UINT64_C(0);
    if (output_id != 0) {
      const auto output = output_xids.find(output_id);
      const auto cached_output = cache.outputs().find(output_id);
      if (output == output_xids.end() || cached_output == cache.outputs().end())
        return {};
      after.primary_output = output->second;
      if (value.policy_result) {
        after.policy_eligible = value.policy_result->eligible != 0;
        after.selected_candidate = value.policy_result->selected != 0;
        after.reason_flags = value.policy_result->reason_flags;
      } else {
        after.policy_eligible = value.compositor_state->policy_eligible != 0;
        after.selected_candidate =
            value.compositor_state->policy_selected != 0;
        after.reason_flags = value.compositor_state->reason_flags;
      }
      after.policy_generation = cache.generation();
      mode = output_policy(cached_output->second.policy.mode);
      if (cached_output->second.compositor_state) {
        after.effective_output_enabled =
            cached_output->second.compositor_state->effective_enabled != 0;
        after.output_state_generation =
            cached_output->second.compositor_state->state_generation;
      }
    }
    batch.windows.push_back({window_id, before, after, mode});
  }
  return batch;
}

std::vector<VrrNotification> publish_vrr_event_batch(
    VrrWindowStateStore& published, const VrrEventBatch& batch,
    const gw::protocol::x11::ByteOrder order,
    const std::uint64_t sequence) {
  std::vector<VrrNotification> notifications;
  for (const auto& transition : batch.windows) {
    auto after = transition.after;
    // Selections are server-owned and must survive a projected state update.
    if (const auto* current = published.find_window(transition.window_id))
      after.event_selections = current->event_selections;
    auto emitted = extensions::gw_vrr_notifications(
        order, sequence, transition.window_id, transition.before, after,
        transition.output_policy);
    notifications.insert(notifications.end(),
                         std::make_move_iterator(emitted.begin()),
                         std::make_move_iterator(emitted.end()));
  }
  apply_vrr_event_batch(published, batch);
  return notifications;
}

void apply_vrr_event_batch(VrrWindowStateStore& published,
                           const VrrEventBatch& batch) {
  for (const auto& [xid, output] : batch.outputs)
    published.ensure_output(xid) = output;
  for (const auto& transition : batch.windows) {
    auto after = transition.after;
    if (const auto* current = published.find_window(transition.window_id))
      after.event_selections = current->event_selections;
    published.ensure_window(transition.window_id) = std::move(after);
  }
}

void enqueue_vrr_event_batch_notifications(
    const VrrWindowStateStore& published, const VrrEventBatch& batch,
    const std::span<ClientConnection* const> recipients) {
  for (const auto& transition : batch.windows) {
    const auto* after = published.find_window(transition.window_id);
    if (!after) continue;
    const auto changed = vrr_change_mask(transition.before, *after);
    for (auto* recipient : recipients) {
      const auto selection =
          after->event_selections.find(recipient->identifier());
      if (selection == after->event_selections.end()) continue;
      const auto selected = changed & selection->second & kKnownVrrEventMask;
      if (selected != 0)
        (void)recipient->enqueue_server_packet(extensions::encode_gw_vrr_notify(
            recipient->byte_order(), recipient->last_request_sequence(),
            selected, transition.window_id, *after,
            transition.output_policy));
    }
  }
}

}  // namespace glasswyrm::server
