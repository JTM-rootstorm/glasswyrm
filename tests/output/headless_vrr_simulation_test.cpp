#include "backends/headless/inventory.hpp"
#include "backends/headless/vrr_simulation.hpp"

#include "output/vrr/reasons.hpp"
#include "tests/helpers/test_support.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using glasswyrm::headless::OutputInventory;
using glasswyrm::headless::OutputRequest;
using glasswyrm::headless::VrrSimulation;
using glasswyrm::headless::VrrSimulationRequest;

OutputInventory inventory(std::vector<OutputRequest> outputs) {
  std::string error;
  auto result = OutputInventory::build(outputs, error);
  gw::test::require(result.has_value(), error);
  return std::move(*result);
}

void rejects_bad_configuration() {
  auto outputs = inventory({OutputRequest{"LEFT", 800, 600, 60'000}});
  std::string error;
  gw::test::require(
      !VrrSimulation::build(
          outputs.layout(),
          std::vector{VrrSimulationRequest{"MISSING", 40'000, 60'000}},
          error),
      "simulation rejects an unknown output");
  gw::test::require(error.find("does not exist") != std::string::npos,
                    "unknown-output rejection is diagnostic");
  gw::test::require(
      !VrrSimulation::build(
          outputs.layout(),
          std::vector{VrrSimulationRequest{"LEFT", 60'000, 60'000}},
          error),
      "simulation rejects an empty range");
  gw::test::require(
      !VrrSimulation::build(
          outputs.layout(),
          std::vector{VrrSimulationRequest{"LEFT", 40'000, 61'000}},
          error),
      "simulation rejects a range above nominal refresh");
  gw::test::require(
      !VrrSimulation::build(
          outputs.layout(),
          std::vector{VrrSimulationRequest{"LEFT", 40'000, 60'000},
                      VrrSimulationRequest{"LEFT", 48'000, 60'000}},
          error),
      "simulation rejects duplicate output configuration");
}

void reports_simulation_without_hardware_claim() {
  auto outputs = inventory({OutputRequest{"LEFT", 800, 600, 60'000}});
  std::string error;
  auto simulation = VrrSimulation::build(
      outputs.layout(),
      std::vector{VrrSimulationRequest{"LEFT", 40'000, 60'000}}, error);
  gw::test::require(simulation.has_value(), error);
  const auto output_id = outputs.layout().output_order.front();
  const auto capability = simulation->capability(output_id);
  gw::test::require(
      capability && capability->connector_property_present &&
          !capability->hardware_capable && capability->kms_controllable &&
          capability->simulated && capability->range_available &&
          capability->atomic_required &&
          capability->minimum_refresh_millihertz == 40'000 &&
          capability->maximum_refresh_millihertz == 60'000,
      "simulated capability is controllable but never hardware capable");
  const auto facts = simulation->output_facts(output_id);
  gw::test::require(facts.enabled && facts.connected && !facts.drm &&
                        !facts.hardware_capable &&
                        !facts.atomic_kms_available &&
                        facts.vrr_property_present && facts.atomic_test_passed &&
                        facts.kms_controllable && facts.simulated,
                    "decision facts identify the separate simulated path");
}

void produces_independent_deterministic_timelines() {
  auto outputs = inventory({OutputRequest{"LEFT", 800, 600, 60'000},
                            OutputRequest{"RIGHT", 640, 480, 75'000}});
  std::string error;
  auto simulation = VrrSimulation::build(
      outputs.layout(),
      std::vector{VrrSimulationRequest{"LEFT", 40'000, 60'000},
                  VrrSimulationRequest{"RIGHT", 48'000, 75'000}},
      error);
  gw::test::require(simulation.has_value(), error);
  const auto left = outputs.layout().output_order[0];
  const auto right = outputs.layout().output_order[1];
  constexpr std::uint64_t game_interval = 20'000'000;

  const auto left_on =
      simulation->present(left, true, game_interval, 1, 10, error);
  const auto right_off = simulation->present(right, false, 0, 1, 10, error);
  const auto left_off = simulation->present(left, false, 0, 2, 11, error);
  gw::test::require(left_on && right_off && left_off, error);
  gw::test::require(
      left_on->effective_enabled && left_on->interval_nanoseconds == game_interval &&
          left_on->flip_sequence == 1 &&
          left_on->kernel_timestamp_nanoseconds == game_interval &&
          left_on->simulated &&
          glasswyrm::output::vrr::has_reason(
              left_on->reasons,
              glasswyrm::output::vrr::Reason::SimulatedHeadless),
      "enabled simulation follows the fixture cadence synchronously");
  gw::test::require(
      !right_off->effective_enabled && right_off->flip_sequence == 1 &&
          right_off->interval_nanoseconds ==
              glasswyrm::headless::refresh_interval_nanoseconds(75'000),
      "independent output uses its nominal disabled cadence");
  gw::test::require(
      !left_off->effective_enabled && left_off->flip_sequence == 2 &&
          left_off->interval_nanoseconds ==
              glasswyrm::headless::refresh_interval_nanoseconds(60'000) &&
          left_off->kernel_timestamp_nanoseconds ==
              game_interval + left_off->interval_nanoseconds,
      "state transition advances only the selected output timeline");
}

} // namespace

int main() {
  rejects_bad_configuration();
  reports_simulation_without_hardware_claim();
  produces_independent_deterministic_timelines();
}
