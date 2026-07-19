#include "backends/headless/vrr_simulation.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <utility>

namespace glasswyrm::headless {
namespace {

constexpr std::uint64_t kNanosecondsPerMillihertz = UINT64_C(1'000'000'000'000);

} // namespace

std::uint64_t
refresh_interval_nanoseconds(const std::uint32_t refresh_millihertz) noexcept {
  if (refresh_millihertz == 0)
    return 0;
  return (kNanosecondsPerMillihertz + refresh_millihertz / 2U) /
         refresh_millihertz;
}

std::optional<VrrSimulation>
VrrSimulation::build(const output::OutputLayout &layout,
                     const std::span<const VrrSimulationRequest> requests,
                     std::string &error) {
  error.clear();
  std::map<output::OutputId, State> states;
  std::set<std::string> names;
  for (const auto &request : requests) {
    if (request.name.empty() ||
        request.minimum_refresh_millihertz == 0 ||
        request.minimum_refresh_millihertz >=
            request.maximum_refresh_millihertz) {
      error = "headless VRR range requires NAME with 0 < minimum < maximum";
      return std::nullopt;
    }
    if (!names.insert(request.name).second) {
      error = "headless VRR output names must be unique";
      return std::nullopt;
    }

    const auto descriptor = std::find_if(
        layout.descriptors.begin(), layout.descriptors.end(),
        [&request](const auto &entry) {
          return entry.second.name == request.name;
        });
    if (descriptor == layout.descriptors.end()) {
      error = "headless VRR output does not exist in the output inventory";
      return std::nullopt;
    }
    const auto state = layout.states.find(descriptor->first);
    if (state == layout.states.end() || state->second.refresh_millihertz == 0) {
      error = "headless VRR output has no enabled nominal refresh";
      return std::nullopt;
    }
    if (request.maximum_refresh_millihertz >
        state->second.refresh_millihertz) {
      error = "headless VRR maximum must not exceed nominal output refresh";
      return std::nullopt;
    }

    VrrSimulationCapability capability;
    capability.output_id = descriptor->first;
    capability.minimum_refresh_millihertz =
        request.minimum_refresh_millihertz;
    capability.maximum_refresh_millihertz =
        request.maximum_refresh_millihertz;
    states.emplace(descriptor->first,
                   State{capability, state->second.refresh_millihertz});
  }
  return VrrSimulation(std::move(states));
}

bool VrrSimulation::configured(const output::OutputId output_id) const noexcept {
  return states_.contains(output_id);
}

std::optional<VrrSimulationCapability>
VrrSimulation::capability(const output::OutputId output_id) const noexcept {
  const auto found = states_.find(output_id);
  if (found == states_.end())
    return std::nullopt;
  return found->second.capability;
}

output::vrr::OutputFacts
VrrSimulation::output_facts(const output::OutputId output_id) const noexcept {
  output::vrr::OutputFacts facts;
  if (!configured(output_id))
    return facts;
  facts.enabled = true;
  facts.connected = true;
  facts.vrr_property_present = true;
  facts.atomic_test_passed = true;
  facts.kms_controllable = true;
  facts.simulated = true;
  return facts;
}

std::optional<VrrSimulationPresentation> VrrSimulation::present(
    const output::OutputId output_id, const bool desired_enabled,
    const std::uint64_t target_interval_nanoseconds,
    const std::uint64_t commit_id, const std::uint64_t presented_generation,
    std::string &error) {
  error.clear();
  auto found = states_.find(output_id);
  if (found == states_.end()) {
    error = "headless VRR presentation references an unconfigured output";
    return std::nullopt;
  }
  if (commit_id == 0 || presented_generation == 0) {
    error = "headless VRR presentation requires nonzero commit and generation";
    return std::nullopt;
  }
  auto &state = found->second;
  const auto interval = desired_enabled
                            ? target_interval_nanoseconds
                            : refresh_interval_nanoseconds(
                                  state.nominal_refresh_millihertz);
  if (interval == 0 ||
      state.timestamp_nanoseconds >
          std::numeric_limits<std::uint64_t>::max() - interval) {
    error = "headless VRR presentation interval is invalid";
    return std::nullopt;
  }

  state.effective_enabled = desired_enabled;
  ++state.sequence;
  state.timestamp_nanoseconds += interval;
  return VrrSimulationPresentation{
      output_id,
      commit_id,
      presented_generation,
      state.sequence,
      state.timestamp_nanoseconds,
      interval,
      state.effective_enabled};
}

} // namespace glasswyrm::headless
