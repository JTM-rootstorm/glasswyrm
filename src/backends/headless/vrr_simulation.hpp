#pragma once

#include "output/model/types.hpp"
#include "output/vrr/decision.hpp"
#include "output/vrr/reasons.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace glasswyrm::headless {

struct VrrSimulationRequest {
  std::string name;
  std::uint32_t minimum_refresh_millihertz{};
  std::uint32_t maximum_refresh_millihertz{};

  friend bool operator==(const VrrSimulationRequest &,
                         const VrrSimulationRequest &) = default;
};

struct VrrSimulationCapability {
  output::OutputId output_id{};
  bool connector_property_present{true};
  bool hardware_capable{false};
  bool kms_controllable{true};
  bool simulated{true};
  bool range_available{true};
  bool atomic_required{true};
  std::uint32_t minimum_refresh_millihertz{};
  std::uint32_t maximum_refresh_millihertz{};
};

struct VrrSimulationPresentation {
  output::OutputId output_id{};
  std::uint64_t commit_id{};
  std::uint64_t presented_generation{};
  std::uint32_t flip_sequence{};
  std::uint64_t kernel_timestamp_nanoseconds{};
  std::uint64_t interval_nanoseconds{};
  bool effective_enabled{};
  bool timestamp_available{true};
  bool simulated{true};
  output::vrr::ReasonMask reasons{
      output::vrr::reason_bit(output::vrr::Reason::SimulatedHeadless)};
};

class VrrSimulation final {
public:
  VrrSimulation(const VrrSimulation &) = default;
  VrrSimulation &operator=(const VrrSimulation &) = default;
  VrrSimulation(VrrSimulation &&) noexcept = default;
  VrrSimulation &operator=(VrrSimulation &&) noexcept = default;

  [[nodiscard]] static std::optional<VrrSimulation>
  build(const output::OutputLayout &layout,
        std::span<const VrrSimulationRequest> requests, std::string &error);

  [[nodiscard]] bool configured(output::OutputId output_id) const noexcept;
  [[nodiscard]] std::optional<VrrSimulationCapability>
  capability(output::OutputId output_id) const noexcept;
  [[nodiscard]] output::vrr::OutputFacts
  output_facts(output::OutputId output_id) const noexcept;

  [[nodiscard]] std::optional<VrrSimulationPresentation>
  present(output::OutputId output_id, bool desired_enabled,
          std::uint64_t target_interval_nanoseconds, std::uint64_t commit_id,
          std::uint64_t presented_generation, std::string &error);

private:
  struct State {
    VrrSimulationCapability capability;
    std::uint32_t nominal_refresh_millihertz{};
    bool effective_enabled{};
    std::uint32_t sequence{};
    std::uint64_t timestamp_nanoseconds{};
  };

  explicit VrrSimulation(std::map<output::OutputId, State> states)
      : states_(std::move(states)) {}

  std::map<output::OutputId, State> states_;
};

[[nodiscard]] std::uint64_t
refresh_interval_nanoseconds(std::uint32_t refresh_millihertz) noexcept;

} // namespace glasswyrm::headless
