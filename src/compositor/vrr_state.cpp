#include "compositor/vrr_state.hpp"

#include <algorithm>
#include <iterator>

namespace gw::compositor {
namespace {

bool zero_reserved(const std::uint64_t (&values)[4]) noexcept {
  return std::ranges::all_of(values,
                             [](const auto value) { return value == 0; });
}

bool boolean(const std::uint8_t value) noexcept { return value <= 1; }

bool valid_mode(const gwipc_vrr_policy_mode mode) noexcept {
  return mode >= GWIPC_VRR_POLICY_OFF &&
         mode <= GWIPC_VRR_POLICY_ALWAYS_ELIGIBLE;
}

bool valid_preference(const gwipc_vrr_window_preference preference) noexcept {
  return preference >= GWIPC_VRR_PREFERENCE_DEFAULT &&
         preference <= GWIPC_VRR_PREFERENCE_PREFER;
}

} // namespace

bool valid_output_vrr_policy(
    const gwipc_output_vrr_policy_upsert& value) noexcept {
  return value.struct_size >= sizeof(value) && value.output_id != 0 &&
         valid_mode(value.mode) && value.flags == 0 &&
         zero_reserved(value.reserved);
}

bool valid_surface_vrr_state(const gwipc_surface_vrr_state& value) noexcept {
  return value.struct_size >= sizeof(value) && value.surface_id != 0 &&
         value.window_id != 0 &&
         value.output_id != 0 && valid_preference(value.preference) &&
         boolean(value.policy_selected) && boolean(value.policy_eligible) &&
         boolean(value.focused) && boolean(value.fullscreen) &&
         boolean(value.borderless_fullscreen) &&
         boolean(value.exclusive_output_membership) && value.reserved16 == 0 &&
         (value.reason_flags & ~GWIPC_VRR_KNOWN_REASON_MASK) == 0 &&
         value.policy_generation != 0 && value.flags == 0 &&
         zero_reserved(value.reserved);
}

bool CommittedVrrState::promote(OutputStateMap states, TimingMap timings,
                                const std::uint64_t commit_id,
                                const std::uint64_t presented_generation,
                                std::string& error) {
  if (commit_id == 0 || presented_generation == 0 || states.empty() ||
      states.size() != timings.size()) {
    error = "VRR presentation result is incomplete";
    return false;
  }
  for (const auto& [output_id, state] : states) {
    const auto timing = timings.find(output_id);
    if (state.output_id != output_id || state.last_commit_id != commit_id ||
        state.last_presented_generation != presented_generation ||
        timing == timings.end() || timing->second.output_id != output_id ||
        timing->second.commit_id != commit_id ||
        timing->second.presented_generation != presented_generation) {
      error = "VRR presentation result does not match its frame";
      return false;
    }
  }
  outputs_ = std::move(states);
  timings_ = std::move(timings);
  error.clear();
  return true;
}

void CommittedVrrState::clear() noexcept {
  outputs_.clear();
  timings_.clear();
}

} // namespace gw::compositor
