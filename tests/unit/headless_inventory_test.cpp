#include "backends/headless/inventory.hpp"

#include "output/model/identity.hpp"
#include "output/model/layout.hpp"
#include "tests/helpers/test_support.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using glasswyrm::headless::OutputInventory;
using glasswyrm::headless::OutputRequest;
using glasswyrm::output::OutputKind;
using glasswyrm::output::OutputLayout;
using glasswyrm::output::OutputTransform;

static_assert(std::is_same_v<
              decltype(std::declval<const OutputInventory&>().layout()),
              const OutputLayout&>);

OutputInventory require_inventory(std::span<const OutputRequest> requests) {
  std::string error;
  auto inventory = OutputInventory::build(requests, error);
  gw::test::require(inventory.has_value(), error);
  gw::test::require(error.empty(), "successful inventory clears its error");
  return std::move(*inventory);
}

void require_rejected(std::span<const OutputRequest> requests,
                      const std::string& expected_error) {
  std::string error{"stale"};
  const auto inventory = OutputInventory::build(requests, error);
  gw::test::require(!inventory, "invalid inventory must be rejected");
  gw::test::require(error.find(expected_error) != std::string::npos,
                    "inventory rejection must explain the bounded invariant");
}

} // namespace

int main() {
  using namespace glasswyrm;

  auto historical = require_inventory(std::span<const OutputRequest>{});
  const auto& historical_layout = historical.layout();
  gw::test::require(historical.uses_historical_default(),
                    "absent CLI option selects historical inventory");
  gw::test::require(historical_layout.descriptors.size() == 1 &&
                        historical_layout.states.size() == 1 &&
                        historical_layout.output_order.size() == 1,
                    "historical inventory contains exactly one output");
  gw::test::require(historical_layout.root_logical_width == 1024 &&
                        historical_layout.root_logical_height == 768 &&
                        historical_layout.enabled_output_count == 1 &&
                        historical_layout.generation == 1,
                    "historical geometry and generation remain fixed");

  const auto default_id =
      output::derive_headless_output_id(headless::kDefaultOutputName);
  gw::test::require(default_id &&
                        historical_layout.primary_output_id == *default_id &&
                        historical_layout.output_order.front() == *default_id,
                    "historical output has the stable default identity");
  const auto& default_descriptor =
      historical_layout.descriptors.at(*default_id);
  const auto& default_state = historical_layout.states.at(*default_id);
  gw::test::require(default_descriptor.name == "HEADLESS-1" &&
                        default_descriptor.kind == OutputKind::Headless &&
                        default_descriptor.connected &&
                        default_descriptor.physical_width_mm == 0 &&
                        default_descriptor.physical_height_mm == 0 &&
                        default_descriptor.supported_transform_mask ==
                            output::kAllOutputTransformsMask &&
                        default_descriptor.mode_configurable &&
                        default_descriptor.scale_configurable &&
                        default_descriptor.transform_configurable &&
                        default_descriptor.primary_eligible &&
                        default_descriptor.arbitrary_headless_mode,
                    "headless descriptor publishes configurable capabilities");
  gw::test::require(default_descriptor.modes.size() == 1,
                    "default descriptor has one deterministic current mode");
  const auto& default_mode = default_descriptor.modes.front();
  const auto expected_default_mode_id = output::derive_output_mode_id(
      *default_id, 1024, 768, 60'000, 0, "1024x768@60000");
  gw::test::require(expected_default_mode_id &&
                        default_mode.id == *expected_default_mode_id &&
                        default_mode.output_id == *default_id &&
                        default_mode.physical_width == 1024 &&
                        default_mode.physical_height == 768 &&
                        default_mode.refresh_millihertz == 60'000 &&
                        default_mode.name == "1024x768@60000" &&
                        default_mode.preferred && default_mode.current,
                    "default mode identity is stable and fully specified");
  gw::test::require(default_state.enabled && default_state.primary &&
                        default_state.mode_id == default_mode.id &&
                        default_state.logical_x == 0 &&
                        default_state.logical_y == 0 &&
                        default_state.logical_width == 1024 &&
                        default_state.logical_height == 768 &&
                        default_state.physical_width == 1024 &&
                        default_state.physical_height == 768 &&
                        default_state.refresh_millihertz == 60'000 &&
                        default_state.scale == output::RationalScale{1, 1} &&
                        default_state.transform == OutputTransform::Normal &&
                        default_state.generation == 1,
                    "default current state retains historical dimensions");
  gw::test::require(static_cast<bool>(
                        output::validate_layout(historical_layout)),
                    "historical inventory passes shared layout validation");

  const std::vector requests{
      OutputRequest{"LEFT", 800, 600, 59'940},
      OutputRequest{"RIGHT", 1024, 768, 60'000},
  };
  auto inventory = require_inventory(requests);
  const auto& layout = inventory.layout();
  gw::test::require(!inventory.uses_historical_default(),
                    "explicit outputs select the output-model inventory");
  gw::test::require(layout.root_logical_width == 1824 &&
                        layout.root_logical_height == 768 &&
                        layout.enabled_output_count == 2 &&
                        layout.output_order.size() == 2,
                    "explicit outputs are laid out left-to-right");

  const auto left_id = output::derive_headless_output_id("LEFT");
  const auto right_id = output::derive_headless_output_id("RIGHT");
  gw::test::require(left_id && right_id &&
                        layout.output_order[0] == *left_id &&
                        layout.output_order[1] == *right_id &&
                        layout.primary_output_id == *left_id,
                    "CLI order determines deterministic order and primary");
  const auto& left = layout.states.at(*left_id);
  const auto& right = layout.states.at(*right_id);
  gw::test::require(left.logical_x == 0 && left.logical_y == 0 &&
                        left.logical_width == 800 &&
                        left.logical_height == 600 && left.primary &&
                        right.logical_x == 800 && right.logical_y == 0 &&
                        right.logical_width == 1024 &&
                        right.logical_height == 768 && !right.primary,
                    "initial output rectangles are gapless and nonoverlapping");
  gw::test::require(static_cast<bool>(output::validate_layout(layout)),
                    "explicit inventory passes shared layout validation");

  auto repeated = require_inventory(requests);
  gw::test::require(
      repeated.layout().output_order == layout.output_order &&
          repeated.layout().descriptors.at(*left_id).modes.front().id ==
              layout.descriptors.at(*left_id).modes.front().id &&
          repeated.layout().descriptors.at(*right_id).modes.front().id ==
              layout.descriptors.at(*right_id).modes.front().id,
      "identical inventory input produces identical output and mode IDs");
  const std::vector reversed{requests[1], requests[0]};
  auto reordered = require_inventory(reversed);
  gw::test::require(
      reordered.layout().descriptors.at(*left_id).id == *left_id &&
          reordered.layout().descriptors.at(*right_id).id == *right_id &&
          reordered.layout().descriptors.at(*left_id).modes.front().id ==
              layout.descriptors.at(*left_id).modes.front().id &&
          reordered.layout().descriptors.at(*right_id).modes.front().id ==
              layout.descriptors.at(*right_id).modes.front().id,
      "stable identities do not depend on CLI ordering");

  std::vector<OutputRequest> maximum;
  for (std::uint32_t index = 0; index < output::kMaximumOutputs; ++index)
    maximum.push_back(
        {"H" + std::to_string(index + 1), 4000, 1, 60'000});
  auto maximum_inventory = require_inventory(maximum);
  gw::test::require(maximum_inventory.layout().root_logical_width == 32'000 &&
                        maximum_inventory.layout().descriptors.size() == 8,
                    "eight bounded outputs are accepted");
  maximum.push_back({"H9", 1, 1, 60'000});
  require_rejected(maximum, "at most 8");

  require_rejected(
      std::vector{OutputRequest{"DUP", 1, 1, 60'000},
                  OutputRequest{"DUP", 2, 2, 60'000}},
      "unique");
  require_rejected(std::vector{OutputRequest{"-INVALID", 1, 1, 60'000}},
                   "ASCII identifier");
  require_rejected(
      std::vector{OutputRequest{std::string(64, 'A'), 1, 1, 60'000}},
      "ASCII identifier");
  require_rejected(std::vector{OutputRequest{"ZERO-W", 0, 1, 60'000}},
                   "dimensions");
  require_rejected(std::vector{OutputRequest{"WIDE", 4097, 1, 60'000}},
                   "dimensions");
  require_rejected(std::vector{OutputRequest{"NO-HZ", 1, 1, 0}}, "refresh");

  std::vector<OutputRequest> excessive_root;
  for (std::uint32_t index = 0; index < output::kMaximumOutputs; ++index)
    excessive_root.push_back(
        {"R" + std::to_string(index + 1), 4096, 1, 60'000});
  require_rejected(excessive_root, "invalid-root-extent");

  return 0;
}
