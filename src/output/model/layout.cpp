#include "output/model/layout.hpp"

#include "output/model/scale.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <string_view>
#include <tuple>
#include <vector>

namespace glasswyrm::output {
namespace {

LayoutValidationResult failure(const LayoutValidationError error,
                               const OutputId output_id = {}) noexcept {
  return {error, output_id};
}

bool checked_product(const std::uint32_t width, const std::uint32_t height,
                     std::uint64_t &product) noexcept {
  product = static_cast<std::uint64_t>(width) * height;
  return width == 0 || product / width == height;
}

bool valid_name(const std::string_view name) noexcept {
  return !name.empty() && name.size() <= kMaximumOutputNameBytes;
}

bool valid_transform(const OutputTransform transform) noexcept {
  return static_cast<std::uint8_t>(transform) <=
         static_cast<std::uint8_t>(OutputTransform::Flipped270);
}

bool transform_swaps_extents(const OutputTransform transform) noexcept {
  return transform == OutputTransform::Rotate90 ||
         transform == OutputTransform::Rotate270 ||
         transform == OutputTransform::Flipped90 ||
         transform == OutputTransform::Flipped270;
}

bool output_rectangles_overlap(const OutputState &left,
                               const OutputState &right) noexcept {
  const auto left_x1 =
      static_cast<std::uint64_t>(left.logical_x) + left.logical_width;
  const auto left_y1 =
      static_cast<std::uint64_t>(left.logical_y) + left.logical_height;
  const auto right_x1 =
      static_cast<std::uint64_t>(right.logical_x) + right.logical_width;
  const auto right_y1 =
      static_cast<std::uint64_t>(right.logical_y) + right.logical_height;
  return static_cast<std::uint64_t>(left.logical_x) < right_x1 &&
         static_cast<std::uint64_t>(right.logical_x) < left_x1 &&
         static_cast<std::uint64_t>(left.logical_y) < right_y1 &&
         static_cast<std::uint64_t>(right.logical_y) < left_y1;
}

bool descriptor_limits_are_valid(const OutputDescriptor &descriptor) noexcept {
  if (descriptor.maximum_physical_width == 0 ||
      descriptor.maximum_physical_height == 0 ||
      descriptor.maximum_physical_pixels == 0 ||
      descriptor.maximum_physical_width > kMaximumPhysicalExtent ||
      descriptor.maximum_physical_height > kMaximumPhysicalExtent ||
      descriptor.maximum_physical_pixels > kMaximumPhysicalPixels ||
      descriptor.maximum_scale_denominator == 0 ||
      descriptor.maximum_scale_denominator > kMaximumScaleDenominator ||
      !valid_output_scale(descriptor.minimum_scale,
                          descriptor.maximum_scale_denominator) ||
      !valid_output_scale(descriptor.maximum_scale,
                          descriptor.maximum_scale_denominator) ||
      !scale_in_range(descriptor.minimum_scale, RationalScale{1, 1},
                      descriptor.maximum_scale) ||
      !scale_in_range(descriptor.maximum_scale, descriptor.minimum_scale,
                      RationalScale{4, 1}))
    return false;
  return true;
}

bool descriptor_metadata_is_valid(const OutputDescriptor &descriptor) noexcept {
  const bool physical_dimensions_known =
      descriptor.physical_width_mm != 0 && descriptor.physical_height_mm != 0;
  const bool physical_dimensions_absent =
      descriptor.physical_width_mm == 0 && descriptor.physical_height_mm == 0;
  if ((!physical_dimensions_known && !physical_dimensions_absent) ||
      (descriptor.supported_transform_mask & ~kAllOutputTransformsMask) != 0 ||
      descriptor.supported_transform_mask == 0)
    return false;
  switch (descriptor.kind) {
  case OutputKind::Headless:
    return !descriptor.arbitrary_headless_mode || descriptor.mode_configurable;
  case OutputKind::Drm:
    return !descriptor.arbitrary_headless_mode;
  }
  return false;
}

bool valid_descriptor_modes(const OutputDescriptor &descriptor) {
  if (descriptor.modes.size() > kMaximumModesPerOutput)
    return false;
  std::set<OutputModeId> identifiers;
  std::size_t current_count = 0;
  for (const auto &mode : descriptor.modes) {
    std::uint64_t pixels{};
    if (!mode.id || mode.output_id != descriptor.id || mode.name.empty() ||
        mode.name.size() > kMaximumOutputNameBytes ||
        mode.physical_width == 0 || mode.physical_height == 0 ||
        mode.refresh_millihertz == 0 ||
        mode.physical_width > descriptor.maximum_physical_width ||
        mode.physical_height > descriptor.maximum_physical_height ||
        !checked_product(mode.physical_width, mode.physical_height, pixels) ||
        pixels > descriptor.maximum_physical_pixels ||
        !identifiers.insert(mode.id).second)
      return false;
    if (mode.current)
      ++current_count;
  }
  return current_count <= 1;
}

bool state_matches_mode(const OutputDescriptor &descriptor,
                        const OutputState &state) noexcept {
  const auto mode =
      std::ranges::find(descriptor.modes, state.mode_id, &OutputMode::id);
  if (mode != descriptor.modes.end())
    return mode->current && state.physical_width == mode->physical_width &&
           state.physical_height == mode->physical_height &&
           state.refresh_millihertz == mode->refresh_millihertz;

  return descriptor.kind == OutputKind::Headless &&
         descriptor.arbitrary_headless_mode && state.mode_id &&
         state.physical_width != 0 && state.physical_height != 0 &&
         state.refresh_millihertz != 0;
}

std::vector<OutputId> expected_output_order(const OutputLayout &layout) {
  std::vector<OutputId> result;
  result.reserve(layout.states.size());
  for (const auto &[id, state] : layout.states) {
    (void)state;
    result.push_back(id);
  }
  std::ranges::sort(
      result, [&layout](const OutputId left_id, const OutputId right_id) {
        const auto &left = layout.states.at(left_id);
        const auto &right = layout.states.at(right_id);
        if (left.enabled != right.enabled)
          return left.enabled > right.enabled;
        if (!left.enabled)
          return left_id < right_id;
        return std::tie(left.logical_y, left.logical_x, left_id) <
               std::tie(right.logical_y, right.logical_x, right_id);
      });
  return result;
}

LayoutValidationResult validate_inventory_geometry(const OutputLayout &layout,
                                                   std::size_t &enabled_count) {
  if (layout.descriptors.size() > kMaximumOutputs ||
      layout.states.size() > kMaximumOutputs)
    return failure(LayoutValidationError::TooManyOutputs);
  if (layout.descriptors.size() != layout.states.size())
    return failure(LayoutValidationError::IncompleteInventory);
  for (const auto &[id, descriptor] : layout.descriptors) {
    (void)descriptor;
    if (!layout.states.contains(id))
      return failure(LayoutValidationError::IncompleteInventory, id);
  }

  std::size_t primary_count = 0;
  OutputId selected_primary{};
  for (const auto &[id, state] : layout.states) {
    if (!state.enabled)
      continue;
    ++enabled_count;
    if (state.primary) {
      ++primary_count;
      selected_primary = id;
    }
  }
  if (enabled_count == 0)
    return failure(LayoutValidationError::NoEnabledOutput);
  if (primary_count != 1 || !layout.primary_output_id ||
      layout.primary_output_id != selected_primary ||
      !layout.descriptors.at(selected_primary).primary_eligible)
    return failure(LayoutValidationError::InvalidPrimaryOutput,
                   layout.primary_output_id);

  std::set<std::string_view> names;
  std::vector<const OutputState *> enabled;
  enabled.reserve(enabled_count);
  for (const auto &[id, descriptor] : layout.descriptors) {
    const auto state = layout.states.find(id);
    if (!id || descriptor.id != id || state == layout.states.end() ||
        state->second.output_id != id)
      return failure(LayoutValidationError::InvalidIdentity, id);
    if (!valid_name(descriptor.name) || !names.insert(descriptor.name).second)
      return failure(LayoutValidationError::InvalidName, id);
    if (!descriptor_metadata_is_valid(descriptor))
      return failure(LayoutValidationError::InvalidDescriptor, id);
    if (state->second.enabled) {
      if (state->second.logical_x < 0 || state->second.logical_y < 0)
        return failure(LayoutValidationError::InvalidPosition, id);
      enabled.push_back(&state->second);
    }
  }
  for (std::size_t left = 0; left < enabled.size(); ++left)
    for (std::size_t right = left + 1; right < enabled.size(); ++right)
      if (output_rectangles_overlap(*enabled[left], *enabled[right]))
        return failure(LayoutValidationError::OverlappingOutputs,
                       enabled[right]->output_id);

  std::uint64_t root_width = 0;
  std::uint64_t root_height = 0;
  for (const auto *state : enabled) {
    root_width =
        std::max(root_width, static_cast<std::uint64_t>(state->logical_x) +
                                 state->logical_width);
    root_height =
        std::max(root_height, static_cast<std::uint64_t>(state->logical_y) +
                                  state->logical_height);
  }
  if (root_width == 0 || root_height == 0 ||
      root_width > kMaximumRootLogicalWidth ||
      root_height > kMaximumRootLogicalHeight ||
      layout.root_logical_width != root_width ||
      layout.root_logical_height != root_height)
    return failure(LayoutValidationError::InvalidRootExtent);
  return {};
}

LayoutValidationResult validate_scale_mode_state(const OutputLayout &layout) {
  for (const auto &[id, state] : layout.states) {
    const auto &descriptor = layout.descriptors.at(id);
    if (!descriptor_limits_are_valid(descriptor) ||
        !valid_output_scale(state.scale,
                            descriptor.maximum_scale_denominator) ||
        !scale_in_range(state.scale, descriptor.minimum_scale,
                        descriptor.maximum_scale))
      return failure(LayoutValidationError::InvalidScale, id);
  }
  for (const auto &[id, state] : layout.states) {
    const auto &descriptor = layout.descriptors.at(id);
    const auto transform = static_cast<std::uint8_t>(state.transform);
    if (!valid_transform(state.transform) || transform >= 32U ||
        (descriptor.supported_transform_mask & (UINT32_C(1) << transform)) == 0)
      return failure(LayoutValidationError::UnsupportedTransform, id);
  }

  std::uint64_t total_physical_pixels = 0;
  std::set<OutputModeId> mode_identifiers;
  for (const auto &[id, state] : layout.states) {
    const auto &descriptor = layout.descriptors.at(id);
    for (const auto &mode : descriptor.modes)
      if (!mode_identifiers.insert(mode.id).second)
        return failure(LayoutValidationError::InvalidMode, id);
    std::uint64_t pixels{};
    if (!valid_descriptor_modes(descriptor) ||
        state.physical_width > descriptor.maximum_physical_width ||
        state.physical_height > descriptor.maximum_physical_height ||
        !checked_product(state.physical_width, state.physical_height, pixels) ||
        pixels > descriptor.maximum_physical_pixels ||
        (state.enabled &&
         (!descriptor.connected || !state_matches_mode(descriptor, state))))
      return failure(LayoutValidationError::InvalidMode, id);
    if (state.enabled) {
      if (pixels > kMaximumTotalOutputPixels - total_physical_pixels)
        return failure(LayoutValidationError::PhysicalLimitExceeded, id);
      total_physical_pixels += pixels;
    }
  }
  for (const auto &[id, state] : layout.states) {
    if (!state.enabled)
      continue;
    PhysicalExtent transformed{state.physical_width, state.physical_height};
    if (transform_swaps_extents(state.transform))
      std::swap(transformed.width, transformed.height);
    const auto expected = derive_logical_extent(transformed, state.scale);
    if (!expected || expected->width != state.logical_width ||
        expected->height != state.logical_height)
      return failure(LayoutValidationError::InvalidLogicalExtent, id);
  }
  for (const auto &[id, state] : layout.states)
    if (!state.enabled &&
        (state.primary || state.logical_x != 0 || state.logical_y != 0 ||
         state.logical_width != 0 || state.logical_height != 0 ||
         state.physical_width != 0 || state.physical_height != 0 ||
         state.refresh_millihertz != 0))
      return failure(LayoutValidationError::InvalidDisabledState, id);
  return {};
}

LayoutValidationResult validate_metadata(const OutputLayout &layout,
                                         const std::size_t enabled_count) {
  if (layout.generation == 0)
    return failure(LayoutValidationError::InvalidGeneration);
  for (const auto &[id, state] : layout.states)
    if (state.generation != layout.generation)
      return failure(LayoutValidationError::InvalidGeneration, id);
  if (layout.enabled_output_count != enabled_count ||
      layout.output_order != expected_output_order(layout))
    return failure(LayoutValidationError::InvalidOutputOrder);
  return {};
}

} // namespace

LayoutValidationResult validate_layout(const OutputLayout &layout) {
  std::size_t enabled_count = 0;
  const auto inventory = validate_inventory_geometry(layout, enabled_count);
  if (!inventory)
    return inventory;
  const auto state = validate_scale_mode_state(layout);
  if (!state)
    return state;
  return validate_metadata(layout, enabled_count);
}

const char *
layout_validation_error_name(const LayoutValidationError error) noexcept {
  switch (error) {
  case LayoutValidationError::None:
    return "none";
  case LayoutValidationError::TooManyOutputs:
    return "too-many-outputs";
  case LayoutValidationError::IncompleteInventory:
    return "incomplete-inventory";
  case LayoutValidationError::NoEnabledOutput:
    return "no-enabled-output";
  case LayoutValidationError::InvalidPrimaryOutput:
    return "invalid-primary-output";
  case LayoutValidationError::InvalidIdentity:
    return "invalid-identity";
  case LayoutValidationError::InvalidName:
    return "invalid-name";
  case LayoutValidationError::InvalidDescriptor:
    return "invalid-descriptor";
  case LayoutValidationError::InvalidPosition:
    return "invalid-position";
  case LayoutValidationError::OverlappingOutputs:
    return "overlapping-outputs";
  case LayoutValidationError::InvalidRootExtent:
    return "invalid-root-extent";
  case LayoutValidationError::InvalidScale:
    return "invalid-scale";
  case LayoutValidationError::UnsupportedTransform:
    return "unsupported-transform";
  case LayoutValidationError::InvalidMode:
    return "invalid-mode";
  case LayoutValidationError::InvalidLogicalExtent:
    return "invalid-logical-extent";
  case LayoutValidationError::InvalidDisabledState:
    return "invalid-disabled-state";
  case LayoutValidationError::InvalidGeneration:
    return "invalid-generation";
  case LayoutValidationError::InvalidOutputOrder:
    return "invalid-output-order";
  case LayoutValidationError::PhysicalLimitExceeded:
    return "physical-limit-exceeded";
  }
  return "unknown";
}

} // namespace glasswyrm::output
