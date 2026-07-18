#include "output/model/layout.hpp"

#include "helpers/test_support.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace {

using namespace glasswyrm::output;
using gw::test::require;

OutputMode mode(const OutputId output_id, const OutputModeId mode_id,
                const std::uint32_t width, const std::uint32_t height,
                std::string name) {
  return {mode_id, output_id,       width, height, 60'000,
          0,       std::move(name), true,  true};
}

OutputDescriptor descriptor(const OutputId id, std::string name,
                            OutputMode value) {
  OutputDescriptor result;
  result.id = id;
  result.name = std::move(name);
  result.connected = true;
  result.mode_configurable = true;
  result.scale_configurable = true;
  result.transform_configurable = true;
  result.primary_eligible = true;
  result.arbitrary_headless_mode = true;
  result.supported_transform_mask = kAllOutputTransformsMask;
  result.modes.push_back(std::move(value));
  return result;
}

OutputState state(const OutputId output_id, const OutputModeId mode_id,
                  const std::int32_t x, const std::uint32_t physical_width,
                  const std::uint32_t physical_height,
                  const std::uint32_t logical_width,
                  const std::uint32_t logical_height, const RationalScale scale,
                  const bool primary) {
  OutputState result;
  result.output_id = output_id;
  result.enabled = true;
  result.mode_id = mode_id;
  result.logical_x = x;
  result.logical_width = logical_width;
  result.logical_height = logical_height;
  result.physical_width = physical_width;
  result.physical_height = physical_height;
  result.refresh_millihertz = 60'000;
  result.scale = scale;
  result.primary = primary;
  result.generation = 7;
  return result;
}

OutputLayout valid_layout() {
  constexpr OutputId first{UINT64_C(0x8000000000000001)};
  constexpr OutputId second{UINT64_C(0x8000000000000002)};
  constexpr OutputModeId first_mode{UINT64_C(0x4000000000000011)};
  constexpr OutputModeId second_mode{UINT64_C(0x4000000000000012)};
  OutputLayout layout;
  layout.descriptors.emplace(
      first, descriptor(first, "M13-Primary",
                        mode(first, first_mode, 800, 600, "800x600")));
  layout.descriptors.emplace(
      second, descriptor(second, "M13-Secondary",
                         mode(second, second_mode, 640, 480, "640x480")));
  layout.states.emplace(first, state(first, first_mode, 0, 800, 600, 640, 480,
                                     RationalScale{5, 4}, true));
  layout.states.emplace(second, state(second, second_mode, 640, 640, 480, 640,
                                      480, RationalScale{1, 1}, false));
  layout.primary_output_id = first;
  layout.root_logical_width = 1280;
  layout.root_logical_height = 480;
  layout.generation = 7;
  layout.enabled_output_count = 2;
  layout.output_order = {first, second};
  return layout;
}

OutputLayout excessive_pixel_layout() {
  OutputLayout layout;
  layout.generation = 3;
  layout.root_logical_width = 5 * 1024;
  layout.root_logical_height = 1024;
  layout.enabled_output_count = 5;
  for (std::uint64_t index = 0; index < 5; ++index) {
    const OutputId output_id{UINT64_C(0x8000000000000100) + index};
    const OutputModeId mode_id{UINT64_C(0x4000000000000100) + index};
    auto output_descriptor =
        descriptor(output_id, "Large-" + std::to_string(index),
                   mode(output_id, mode_id, 4096, 4096, "4096x4096"));
    layout.descriptors.emplace(output_id, std::move(output_descriptor));
    auto output_state =
        state(output_id, mode_id, static_cast<std::int32_t>(index * 1024), 4096,
              4096, 1024, 1024, RationalScale{4, 1}, index == 0);
    output_state.generation = 3;
    layout.states.emplace(output_id, output_state);
    layout.output_order.push_back(output_id);
  }
  layout.primary_output_id = layout.output_order.front();
  return layout;
}

void expect_error(OutputLayout layout, const LayoutValidationError expected,
                  const char *message) {
  require(validate_layout(layout).error == expected, message);
}

} // namespace

int main() {
  auto layout = valid_layout();
  require(static_cast<bool>(validate_layout(layout)),
          "fractional two-output layout passes exact validation");

  auto rotated = layout;
  auto &rotated_descriptor = rotated.descriptors.at(rotated.output_order[1]);
  auto &rotated_mode = rotated_descriptor.modes.front();
  rotated_mode.physical_width = 480;
  rotated_mode.physical_height = 640;
  auto &rotated_state = rotated.states.at(rotated.output_order[1]);
  rotated_state.physical_width = 480;
  rotated_state.physical_height = 640;
  rotated_state.transform = OutputTransform::Rotate90;
  require(static_cast<bool>(validate_layout(rotated)),
          "quarter-turn transform swaps physical extent before scaling");

  auto remainder = layout;
  auto &remainder_mode =
      remainder.descriptors.at(remainder.output_order[0]).modes.front();
  remainder_mode.physical_width = 801;
  remainder_mode.physical_height = 640;
  auto &remainder_state = remainder.states.at(remainder.output_order[0]);
  remainder_state.physical_width = 801;
  remainder_state.physical_height = 640;
  remainder_state.scale = {4, 3};
  remainder_state.logical_width = 601;
  require(static_cast<bool>(validate_layout(remainder)),
          "logical extent uses checked ceiling for a fractional remainder");

  auto changed = layout;
  for (auto &[id, output] : changed.states) {
    (void)id;
    output.enabled = false;
    output.primary = false;
    output.logical_x = 0;
    output.logical_y = 0;
    output.logical_width = 0;
    output.logical_height = 0;
    output.physical_width = 0;
    output.physical_height = 0;
    output.refresh_millihertz = 0;
  }
  changed.primary_output_id = {};
  changed.root_logical_width = 0;
  changed.root_logical_height = 0;
  changed.enabled_output_count = 0;
  expect_error(std::move(changed), LayoutValidationError::NoEnabledOutput,
               "layout requires at least one enabled output");

  changed = layout;
  changed.states.at(changed.output_order[1]).primary = true;
  expect_error(std::move(changed), LayoutValidationError::InvalidPrimaryOutput,
               "layout requires exactly one enabled primary");

  changed = layout;
  changed.descriptors.at(changed.output_order[1]).name = "M13-Primary";
  expect_error(std::move(changed), LayoutValidationError::InvalidName,
               "descriptor names are unique");

  changed = layout;
  changed.states.at(changed.output_order[1]).logical_x = -1;
  expect_error(std::move(changed), LayoutValidationError::InvalidPosition,
               "enabled positions are nonnegative");

  changed = layout;
  changed.states.at(changed.output_order[1]).logical_x = 639;
  expect_error(std::move(changed), LayoutValidationError::OverlappingOutputs,
               "enabled logical rectangles cannot overlap");

  changed = layout;
  changed.states.at(changed.output_order[1]).logical_x = 32'128;
  changed.root_logical_width = 32'768;
  expect_error(std::move(changed), LayoutValidationError::InvalidRootExtent,
               "root logical extent is bounded to protocol geometry limits");

  changed = layout;
  changed.root_logical_width = 1279;
  expect_error(std::move(changed), LayoutValidationError::InvalidRootExtent,
               "root extent must be the exact checked bounding box");

  changed = layout;
  changed.descriptors.at(changed.output_order[0]).primary_eligible = false;
  expect_error(std::move(changed), LayoutValidationError::InvalidPrimaryOutput,
               "primary output must advertise primary eligibility");

  changed = layout;
  changed.states.at(changed.output_order[0]).scale = {10, 8};
  expect_error(std::move(changed), LayoutValidationError::InvalidScale,
               "layout refuses silently reduced scales");

  changed = layout;
  changed.descriptors.at(changed.output_order[1]).supported_transform_mask =
      output_transform_bit(OutputTransform::Normal);
  changed.states.at(changed.output_order[1]).transform =
      OutputTransform::Rotate180;
  expect_error(std::move(changed), LayoutValidationError::UnsupportedTransform,
               "descriptor transform capability is authoritative");

  changed = layout;
  changed.states.at(changed.output_order[1]).physical_width = 641;
  expect_error(std::move(changed), LayoutValidationError::InvalidMode,
               "selected physical state must match its stable mode");

  changed = layout;
  changed.states.at(changed.output_order[0]).logical_width = 639;
  expect_error(
      std::move(changed), LayoutValidationError::InvalidLogicalExtent,
      "logical extent is exactly ceil of transformed pixels over scale");

  changed = layout;
  auto &disabled = changed.states.at(changed.output_order[1]);
  disabled.enabled = false;
  disabled.logical_x = 0;
  disabled.logical_width = 1;
  disabled.logical_height = 0;
  disabled.physical_width = 0;
  disabled.physical_height = 0;
  disabled.refresh_millihertz = 0;
  changed.root_logical_width = 640;
  changed.enabled_output_count = 1;
  changed.output_order = {changed.output_order[0], changed.output_order[1]};
  expect_error(
      std::move(changed), LayoutValidationError::InvalidDisabledState,
      "disabled states require exactly zero logical and physical extents");

  changed = layout;
  changed.generation = 8;
  expect_error(std::move(changed), LayoutValidationError::InvalidGeneration,
               "state and layout generation are one source of truth");

  changed = layout;
  std::swap(changed.output_order[0], changed.output_order[1]);
  expect_error(std::move(changed), LayoutValidationError::InvalidOutputOrder,
               "deterministic output order is logical y/x then identity");

  changed = layout;
  changed.descriptors.at(changed.output_order[0]).id = changed.output_order[1];
  expect_error(std::move(changed), LayoutValidationError::InvalidIdentity,
               "map keys and stable descriptor identities cannot diverge");

  changed = layout;
  for (std::uint64_t index = 2; index < 9; ++index) {
    const OutputId output_id{UINT64_C(0x8000000000000200) + index};
    changed.descriptors.emplace(output_id, OutputDescriptor{});
    changed.states.emplace(output_id, OutputState{});
  }
  expect_error(std::move(changed), LayoutValidationError::TooManyOutputs,
               "layout refuses more than eight outputs before deeper checks");

  expect_error(excessive_pixel_layout(),
               LayoutValidationError::PhysicalLimitExceeded,
               "checked aggregate physical pixel accounting enforces the cap");
  return 0;
}
