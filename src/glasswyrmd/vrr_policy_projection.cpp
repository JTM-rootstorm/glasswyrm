#include "glasswyrmd/vrr_policy_projection.hpp"

#include <algorithm>
#include <set>
#include <string_view>
#include <type_traits>

namespace glasswyrm::server {
namespace {

void hash_byte(std::uint64_t& hash, const std::uint8_t value) noexcept {
  hash = (hash ^ value) * UINT64_C(1099511628211);
}

template <class T>
void hash_little(std::uint64_t& hash, const T value) noexcept {
  using U = std::make_unsigned_t<T>;
  auto bits = static_cast<std::uint64_t>(static_cast<U>(value));
  for (std::size_t index = 0; index < sizeof(T); ++index) {
    hash_byte(hash, static_cast<std::uint8_t>(bits));
    bits >>= 8U;
  }
}

bool valid_bool(const std::uint8_t value) noexcept { return value <= 1; }

bool valid_mode(const gwipc_vrr_policy_mode value) noexcept {
  return value >= GWIPC_VRR_POLICY_OFF &&
         value <= GWIPC_VRR_POLICY_ALWAYS_ELIGIBLE;
}

bool valid_preference(const gwipc_vrr_window_preference value) noexcept {
  return value >= GWIPC_VRR_PREFERENCE_DEFAULT &&
         value <= GWIPC_VRR_PREFERENCE_PREFER;
}

}  // namespace

void synchronize_vrr_windows(const LifecycleSnapshot& snapshot,
                             const VrrWindowStateStore& published,
                             VrrStateCache& cache) {
  std::vector<std::uint32_t> stale;
  for (const auto& [window_id, unused] : cache.windows()) {
    static_cast<void>(unused);
    if (!snapshot.windows.contains(window_id)) stale.push_back(window_id);
  }
  for (const auto window_id : stale) cache.erase_window(window_id);
  for (const auto& [window_id, window] : snapshot.windows) {
    const auto* value = published.find_window(window_id);
    cache.set_window_preference(
        window_id,
        static_cast<gwipc_vrr_window_preference>(
            value ? value->preference : WindowVrrPreference::Default));
    cache.set_window_policy_candidate(window_id, !window.override_redirect);
  }
}

void restore_vrr_lifecycle_checkpoint(
    VrrStateCache& current, const VrrStateCache& checkpoint) noexcept {
  current = checkpoint;
}

VrrPolicyProjection project_vrr_policy(const LifecycleSnapshot& snapshot,
                                       const output::OutputLayout& layout,
                                       const VrrStateCache& cache) {
  VrrPolicyProjection result;
  if (cache.outputs().size() != layout.states.size()) return result;
  result.outputs.reserve(layout.output_order.size());
  for (const auto output_id : layout.output_order) {
    const auto found = cache.outputs().find(output_id.value);
    if (found == cache.outputs().end()) return {};
    const auto& value = found->second;
    gwipc_policy_output_vrr_upsert record{};
    record.struct_size = sizeof(record);
    record.output_id = output_id.value;
    record.mode = value.policy.mode;
    record.hardware_capable = value.capability.hardware_capable;
    record.kms_controllable = value.capability.kms_controllable;
    result.outputs.push_back(record);
  }
  result.windows.reserve(snapshot.windows.size());
  for (const auto& [window_id, window] : snapshot.windows) {
    if (window.override_redirect) continue;
    const auto cached = cache.windows().find(window_id);
    if (cached == cache.windows().end() ||
        !cached->second.policy_candidate)
      return {};
    gwipc_policy_window_vrr_upsert record{};
    record.struct_size = sizeof(record);
    record.window_id = window_id;
    record.preference = cached == cache.windows().end()
                            ? GWIPC_VRR_PREFERENCE_DEFAULT
                            : cached->second.preference;
    result.windows.push_back(record);
    auto membership = window.output_memberships;
    std::ranges::sort(membership);
    membership.erase(std::unique(membership.begin(), membership.end()),
                     membership.end());
    result.memberships.emplace(window_id, std::move(membership));
  }
  return result;
}

std::vector<gwipc_surface_vrr_state> project_vrr_surfaces(
    const LifecycleSnapshot& snapshot, const VrrStateCache& cache,
    const std::uint64_t policy_generation) {
  if (policy_generation == 0 || cache.windows().size() != snapshot.windows.size())
    return {};
  std::vector<gwipc_surface_vrr_state> result;
  result.reserve(snapshot.windows.size());
  for (const auto& [window_id, window] : snapshot.windows) {
    const auto cached = cache.windows().find(window_id);
    if (cached == cache.windows().end() ||
        cached->second.policy_candidate == window.override_redirect)
      return {};
    gwipc_surface_vrr_state record{};
    record.struct_size = sizeof(record);
    record.surface_id = (UINT64_C(1) << 32U) | window_id;
    record.window_id = window_id;
    if (cached->second.policy_candidate) {
      if (!cached->second.policy_result) return {};
      const auto& policy = *cached->second.policy_result;
      record.output_id = policy.output_id;
      record.preference = policy.preference;
      record.policy_selected = policy.selected;
      record.policy_eligible = policy.eligible;
      record.focused = policy.focused;
      record.fullscreen = policy.fullscreen;
      record.borderless_fullscreen = policy.borderless_fullscreen;
      record.exclusive_output_membership =
          policy.exclusive_output_membership;
      record.reason_flags = policy.reason_flags;
    } else {
      if (window.assigned_output_id == 0 ||
          !cache.outputs().contains(window.assigned_output_id))
        return {};
      const bool exclusive = window.output_memberships.size() == 1 &&
                             window.output_memberships.front() ==
                                 window.assigned_output_id;
      record.output_id = window.assigned_output_id;
      record.preference = cached->second.preference;
      record.focused = window.focused;
      record.fullscreen =
          window.applied_state == GWIPC_POLICY_APPLIED_FULLSCREEN;
      record.exclusive_output_membership = exclusive;
      record.reason_flags = GWIPC_VRR_REASON_WINDOW_UNMANAGED;
      if (!window.policy_visible)
        record.reason_flags |= GWIPC_VRR_REASON_WINDOW_HIDDEN;
      if (!window.focused)
        record.reason_flags |= GWIPC_VRR_REASON_WINDOW_UNFOCUSED;
      if (window.output_memberships.size() > 1)
        record.reason_flags |= GWIPC_VRR_REASON_WINDOW_SPANS_OUTPUTS;
      else if (!exclusive)
        record.reason_flags |=
            GWIPC_VRR_REASON_SURFACE_MEMBERSHIP_INVALID;
      if (cached->second.preference == GWIPC_VRR_PREFERENCE_DISABLE)
        record.reason_flags |=
            GWIPC_VRR_REASON_WINDOW_PREFERENCE_DISABLED;
    }
    record.policy_generation = policy_generation;
    result.push_back(record);
  }
  return result;
}

std::uint64_t canonical_vrr_policy_hash(
    const std::uint64_t base_policy_hash, const VrrPolicyProjection& input,
    const std::vector<gwipc_policy_output_vrr_state>& output_results,
    const std::vector<gwipc_policy_window_vrr_state>& window_results) noexcept {
  std::uint64_t hash = UINT64_C(14695981039346656037);
  for (const auto byte : std::string_view{"glasswyrm-policy-v4"})
    hash_byte(hash, static_cast<std::uint8_t>(byte));
  hash_little(hash, base_policy_hash);

  auto outputs = input.outputs;
  std::ranges::sort(outputs, {}, &gwipc_policy_output_vrr_upsert::output_id);
  hash_little(hash, static_cast<std::uint32_t>(outputs.size()));
  for (const auto& output : outputs) {
    hash_little(hash, output.output_id);
    hash_little(hash, static_cast<std::uint16_t>(output.mode));
    hash_little(hash, output.hardware_capable);
    hash_little(hash, output.kms_controllable);
    hash_little(hash, output.flags);
  }

  auto windows = input.windows;
  std::ranges::sort(windows, {}, &gwipc_policy_window_vrr_upsert::window_id);
  hash_little(hash, static_cast<std::uint32_t>(windows.size()));
  for (const auto& window : windows) {
    hash_little(hash, window.window_id);
    hash_little(hash, static_cast<std::uint16_t>(window.preference));
    const auto found = input.memberships.find(window.window_id);
    const std::vector<std::uint64_t> empty;
    const auto& membership = found == input.memberships.end() ? empty
                                                              : found->second;
    hash_little(hash, static_cast<std::uint32_t>(membership.size()));
    for (const auto output_id : membership) hash_little(hash, output_id);
    hash_little(hash, window.flags);
  }

  auto output_states = output_results;
  std::ranges::sort(output_states, {},
                    &gwipc_policy_output_vrr_state::output_id);
  hash_little(hash, static_cast<std::uint32_t>(output_states.size()));
  for (const auto& output : output_states) {
    hash_little(hash, output.output_id);
    hash_little(hash, static_cast<std::uint16_t>(output.mode));
    hash_little(hash, output.selected_window_id);
    hash_little(hash, output.desired_enabled);
    hash_little(hash, output.candidate_required);
    hash_little(hash, output.reason_flags);
    hash_little(hash, output.flags);
  }

  auto window_states = window_results;
  std::ranges::sort(window_states, {},
                    &gwipc_policy_window_vrr_state::window_id);
  hash_little(hash, static_cast<std::uint32_t>(window_states.size()));
  for (const auto& window : window_states) {
    hash_little(hash, window.window_id);
    hash_little(hash, window.output_id);
    hash_little(hash, static_cast<std::uint16_t>(window.preference));
    hash_little(hash, window.selected);
    hash_little(hash, window.eligible);
    hash_little(hash, window.focused);
    hash_little(hash, window.fullscreen);
    hash_little(hash, window.borderless_fullscreen);
    hash_little(hash, window.exclusive_output_membership);
    hash_little(hash, window.reason_flags);
    hash_little(hash, window.flags);
  }
  return hash;
}

bool validate_vrr_policy_result(
    const VrrPolicyProjection& input,
    const std::vector<gwipc_policy_output_vrr_state>& output_results,
    const std::vector<gwipc_policy_window_vrr_state>& window_results) noexcept {
  if (input.outputs.empty() || output_results.size() != input.outputs.size() ||
      window_results.size() != input.windows.size())
    return false;
  std::map<std::uint64_t, gwipc_vrr_policy_mode> expected_outputs;
  for (const auto& output : input.outputs)
    if (output.struct_size < sizeof(output) || output.output_id == 0 ||
        !valid_mode(output.mode) || !valid_bool(output.hardware_capable) ||
        !valid_bool(output.kms_controllable) || output.flags != 0 ||
        !expected_outputs.emplace(output.output_id, output.mode).second)
      return false;
  std::set<std::uint32_t> expected_windows;
  std::map<std::uint32_t, gwipc_vrr_window_preference> preferences;
  for (const auto& window : input.windows)
    if (window.struct_size < sizeof(window) || window.window_id == 0 ||
        !valid_preference(window.preference) || window.flags != 0 ||
        !expected_windows.insert(window.window_id).second)
      return false;
    else
      preferences.emplace(window.window_id, window.preference);

  std::set<std::uint64_t> actual_outputs;
  std::map<std::uint32_t, std::uint64_t> selected;
  for (const auto& output : output_results) {
    const auto expected = expected_outputs.find(output.output_id);
    if (output.struct_size < sizeof(output) ||
        expected == expected_outputs.end() ||
        !actual_outputs.insert(output.output_id).second ||
        output.mode != expected->second ||
        !valid_bool(output.desired_enabled) ||
        !valid_bool(output.candidate_required) || output.flags != 0 ||
        (output.reason_flags & ~GWIPC_VRR_KNOWN_REASON_MASK) != 0 ||
        (output.selected_window_id != 0 &&
         (!expected_windows.contains(output.selected_window_id) ||
          !selected.emplace(output.selected_window_id, output.output_id)
               .second)))
      return false;
  }
  std::set<std::uint32_t> actual_windows;
  for (const auto& window : window_results) {
    const auto preference = preferences.find(window.window_id);
    const auto membership = input.memberships.find(window.window_id);
    if (window.struct_size < sizeof(window) ||
        preference == preferences.end() ||
        !actual_windows.insert(window.window_id).second ||
        !expected_outputs.contains(window.output_id) ||
        window.preference != preference->second ||
        !valid_bool(window.selected) || !valid_bool(window.eligible) ||
        !valid_bool(window.focused) || !valid_bool(window.fullscreen) ||
        !valid_bool(window.borderless_fullscreen) ||
        !valid_bool(window.exclusive_output_membership) || window.flags != 0 ||
        (window.reason_flags & ~GWIPC_VRR_KNOWN_REASON_MASK) != 0 ||
        (window.selected != selected.contains(window.window_id)) ||
        (window.selected &&
         selected.at(window.window_id) != window.output_id) ||
        (window.exclusive_output_membership !=
         (membership != input.memberships.end() &&
          membership->second.size() == 1 &&
          membership->second.front() == window.output_id)))
      return false;
  }
  return true;
}

}  // namespace glasswyrm::server
