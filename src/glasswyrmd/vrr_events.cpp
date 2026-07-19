#include "glasswyrmd/vrr_events.hpp"

#include <algorithm>

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
    if (value.policy_result) {
      const auto& policy = *value.policy_result;
      const auto output = output_xids.find(policy.output_id);
      const auto cached_output = cache.outputs().find(policy.output_id);
      if (output == output_xids.end() || cached_output == cache.outputs().end())
        return {};
      after.primary_output = output->second;
      after.policy_eligible = policy.eligible != 0;
      after.selected_candidate = policy.selected != 0;
      after.reason_flags = policy.reason_flags;
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

}  // namespace glasswyrm::server
