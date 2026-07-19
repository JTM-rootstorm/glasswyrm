#include "backends/headless/inventory.hpp"

#include "output/model/identity.hpp"
#include "output/model/layout.hpp"

#include <set>
#include <utility>
#include <vector>

namespace glasswyrm::headless {
namespace {

constexpr std::uint64_t kInitialInventoryGeneration = 1;

bool valid_request(const OutputRequest &request, std::string &error) {
  if (!valid_output_name(request.name)) {
    error =
        "headless output name must be a 1-63 byte ASCII identifier beginning "
        "with a letter or digit";
    return false;
  }
  const auto pixels =
      static_cast<std::uint64_t>(request.width) * request.height;
  if (request.width == 0 || request.height == 0 ||
      request.width > output::kMaximumPhysicalExtent ||
      request.height > output::kMaximumPhysicalExtent ||
      pixels > output::kMaximumPhysicalPixels) {
    error =
        "headless output dimensions must be within 1..4096 and contain at "
        "most 16777216 pixels";
    return false;
  }
  if (request.refresh_millihertz == 0) {
    error = "headless output refresh must be a positive millihertz value";
    return false;
  }
  return true;
}

std::string mode_name(const OutputRequest &request) {
  return std::to_string(request.width) + "x" +
         std::to_string(request.height) + "@" +
         std::to_string(request.refresh_millihertz);
}

} // namespace

std::optional<OutputInventory>
OutputInventory::build(const std::span<const OutputRequest> requests,
                       std::string &error) {
  error.clear();
  if (requests.size() > output::kMaximumOutputs) {
    error = "headless output inventory supports at most 8 outputs";
    return std::nullopt;
  }

  const OutputRequest historical{
      std::string(kDefaultOutputName), kDefaultOutputWidth,
      kDefaultOutputHeight, kDefaultOutputRefreshMillihertz};
  const auto effective =
      requests.empty() ? std::span<const OutputRequest>(&historical, 1)
                       : requests;

  output::OutputLayout layout;
  layout.generation = kInitialInventoryGeneration;
  layout.enabled_output_count = effective.size();
  layout.output_order.reserve(effective.size());

  std::set<std::string> names;
  std::vector<output::OutputId> output_ids;
  std::vector<output::OutputModeId> mode_ids;
  output_ids.reserve(effective.size());
  mode_ids.reserve(effective.size());

  std::uint32_t logical_x = 0;
  for (std::size_t index = 0; index < effective.size(); ++index) {
    const auto &request = effective[index];
    if (!valid_request(request, error))
      return std::nullopt;
    if (!names.insert(request.name).second) {
      error = "headless output names must be unique";
      return std::nullopt;
    }

    const auto output_id = output::derive_headless_output_id(request.name);
    if (!output_id) {
      error = "headless output identity derivation failed";
      return std::nullopt;
    }
    const auto canonical_mode_name = mode_name(request);
    const auto mode_id = output::derive_output_mode_id(
        *output_id, request.width, request.height, request.refresh_millihertz,
        0, canonical_mode_name);
    if (!mode_id) {
      error = "headless output mode identity derivation failed";
      return std::nullopt;
    }
    output_ids.push_back(*output_id);
    mode_ids.push_back(*mode_id);

    output::OutputMode mode{*mode_id,
                            *output_id,
                            request.width,
                            request.height,
                            request.refresh_millihertz,
                            0,
                            canonical_mode_name,
                            true,
                            true};
    output::OutputDescriptor descriptor;
    descriptor.id = *output_id;
    descriptor.name = request.name;
    descriptor.kind = output::OutputKind::Headless;
    descriptor.connected = true;
    descriptor.supported_transform_mask = output::kAllOutputTransformsMask;
    descriptor.minimum_scale = {1, 1};
    descriptor.maximum_scale = {4, 1};
    descriptor.maximum_scale_denominator = output::kMaximumScaleDenominator;
    descriptor.mode_configurable = true;
    descriptor.scale_configurable = true;
    descriptor.transform_configurable = true;
    descriptor.primary_eligible = true;
    descriptor.arbitrary_headless_mode = true;
    descriptor.maximum_physical_width = output::kMaximumPhysicalExtent;
    descriptor.maximum_physical_height = output::kMaximumPhysicalExtent;
    descriptor.maximum_physical_pixels = output::kMaximumPhysicalPixels;
    descriptor.modes.push_back(mode);

    output::OutputState state;
    state.output_id = *output_id;
    state.enabled = true;
    state.mode_id = *mode_id;
    state.logical_x = static_cast<std::int32_t>(logical_x);
    state.logical_y = 0;
    state.logical_width = request.width;
    state.logical_height = request.height;
    state.physical_width = request.width;
    state.physical_height = request.height;
    state.refresh_millihertz = request.refresh_millihertz;
    state.scale = {1, 1};
    state.transform = output::OutputTransform::Normal;
    state.primary = index == 0;
    state.generation = kInitialInventoryGeneration;

    if (index == 0)
      layout.primary_output_id = *output_id;
    layout.output_order.push_back(*output_id);
    layout.descriptors.emplace(*output_id, std::move(descriptor));
    layout.states.emplace(*output_id, state);
    logical_x += request.width;
  }
  layout.root_logical_width = logical_x;
  layout.root_logical_height = effective.front().height;
  for (const auto &request : effective)
    if (request.height > layout.root_logical_height)
      layout.root_logical_height = request.height;

  if (!output::output_identities_are_unique(output_ids) ||
      !output::mode_identities_are_unique(mode_ids)) {
    error = "headless output stable identity collision";
    return std::nullopt;
  }
  const auto validation = output::validate_layout(layout);
  if (!validation) {
    error = std::string("headless output inventory is invalid: ") +
            output::layout_validation_error_name(validation.error);
    return std::nullopt;
  }
  return OutputInventory(std::move(layout), requests.empty());
}

} // namespace glasswyrm::headless
