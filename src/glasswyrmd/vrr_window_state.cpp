#include "glasswyrmd/vrr_window_state.hpp"

#include "glasswyrmd/resource_table.hpp"

namespace glasswyrm::server {

WindowVrrState* VrrWindowStateStore::find_window(
    const std::uint32_t window) noexcept {
  const auto iterator = windows_.find(window);
  return iterator == windows_.end() ? nullptr : &iterator->second;
}

const WindowVrrState* VrrWindowStateStore::find_window(
    const std::uint32_t window) const noexcept {
  const auto iterator = windows_.find(window);
  return iterator == windows_.end() ? nullptr : &iterator->second;
}

PublishedOutputVrrState* VrrWindowStateStore::find_output(
    const std::uint32_t output) noexcept {
  const auto iterator = outputs_.find(output);
  return iterator == outputs_.end() ? nullptr : &iterator->second;
}

const PublishedOutputVrrState* VrrWindowStateStore::find_output(
    const std::uint32_t output) const noexcept {
  const auto iterator = outputs_.find(output);
  return iterator == outputs_.end() ? nullptr : &iterator->second;
}

WindowVrrState& VrrWindowStateStore::ensure_window(
    const std::uint32_t window) {
  return windows_[window];
}

PublishedOutputVrrState& VrrWindowStateStore::ensure_output(
    const std::uint32_t output) {
  return outputs_[output];
}

void VrrWindowStateStore::erase_window(const std::uint32_t window) noexcept {
  windows_.erase(window);
}

void VrrWindowStateStore::clear_client(const std::uint64_t client) noexcept {
  for (auto& [window, state] : windows_) {
    static_cast<void>(window);
    state.event_selections.erase(client);
  }
}

void VrrWindowStateStore::prune_windows(
    const ResourceTable& resources) noexcept {
  std::erase_if(windows_, [&resources](const auto& entry) {
    return resources.find_window(entry.first) == nullptr;
  });
}

bool valid_vrr_preference(const std::uint16_t value) noexcept {
  return value <= static_cast<std::uint16_t>(WindowVrrPreference::Prefer);
}

std::uint32_t vrr_change_mask(const WindowVrrState& before,
                              const WindowVrrState& after) noexcept {
  std::uint32_t result = 0;
  if (before.preference != after.preference) result |= kVrrPreferenceChanged;
  if (before.policy_eligible != after.policy_eligible ||
      before.selected_candidate != after.selected_candidate ||
      before.primary_output != after.primary_output ||
      before.reason_flags != after.reason_flags ||
      before.policy_generation != after.policy_generation)
    result |= kVrrEligibilityChanged;
  if (before.effective_output_enabled != after.effective_output_enabled ||
      before.output_state_generation != after.output_state_generation)
    result |= kVrrEffectiveStateChanged;
  return result;
}

}  // namespace glasswyrm::server
