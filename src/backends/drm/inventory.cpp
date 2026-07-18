#include "backends/drm/inventory.hpp"

#include "backends/drm/connector_name.hpp"
#include "backends/drm/edid_digest.hpp"
#include "output/model/identity.hpp"
#include "output/model/layout.hpp"

#include <string>
#include <vector>

namespace glasswyrm::drm {
namespace {

constexpr std::uint64_t kInitialInventoryGeneration = 1;

std::optional<output::OutputDescriptor>
build_descriptor(const DeviceSnapshot &snapshot, const Connector &connector,
                 const std::size_t selected_mode_index, bool &edid_participated,
                 std::string &error) {
  const auto name = connector_name(connector.type, connector.type_id);
  const auto &device_identity = snapshot.driver.bus_info.empty()
                                    ? snapshot.sysfs_identity
                                    : snapshot.driver.bus_info;
  if (!connector.edid_digest.empty() &&
      connector.edid_digest.size() != kEdidIdentityDigestBytes) {
    error = "DRM connector EDID identity digest has an unsupported size";
    return std::nullopt;
  }
  const auto identity = output::derive_drm_output_id(
      {device_identity, name, connector.edid_digest});
  if (!identity) {
    error = "DRM output lacks stable device or connector identity";
    return std::nullopt;
  }

  output::OutputDescriptor descriptor;
  descriptor.id = identity->id;
  descriptor.name = name;
  descriptor.kind = output::OutputKind::Drm;
  descriptor.connected = connector.status == ConnectionStatus::Connected;
  descriptor.physical_width_mm = connector.physical_width_mm;
  descriptor.physical_height_mm = connector.physical_height_mm;
  descriptor.supported_transform_mask = output::kAllOutputTransformsMask;
  descriptor.mode_configurable = false;
  descriptor.scale_configurable = true;
  descriptor.transform_configurable = true;
  descriptor.primary_eligible = true;
  descriptor.arbitrary_headless_mode = false;
  descriptor.modes.reserve(connector.modes.size());

  std::vector<output::OutputModeId> mode_ids;
  mode_ids.reserve(connector.modes.size());
  for (std::size_t index = 0; index < connector.modes.size(); ++index) {
    const auto &mode = connector.modes[index];
    const auto mode_id = output::derive_output_mode_id(
        descriptor.id, mode.width, mode.height, mode.refresh_millihz,
        mode.flags, mode.name);
    if (!mode_id) {
      error = "DRM connector exposes a mode without stable identity facts";
      return std::nullopt;
    }
    mode_ids.push_back(*mode_id);
    descriptor.modes.push_back(
        {*mode_id, descriptor.id, mode.width, mode.height, mode.refresh_millihz,
         mode.flags, mode.name, mode.preferred, index == selected_mode_index});
  }
  if (!output::mode_identities_are_unique(mode_ids)) {
    error = "DRM connector exposes colliding stable mode identities";
    return std::nullopt;
  }
  edid_participated = identity->edid_participated;
  return descriptor;
}

output::OutputState build_state(const output::OutputDescriptor &descriptor,
                                const output::OutputMode &mode) {
  output::OutputState state;
  state.output_id = descriptor.id;
  state.enabled = true;
  state.mode_id = mode.id;
  state.logical_width = mode.physical_width;
  state.logical_height = mode.physical_height;
  state.physical_width = mode.physical_width;
  state.physical_height = mode.physical_height;
  state.refresh_millihertz = mode.refresh_millihertz;
  state.scale = {1, 1};
  state.transform = output::OutputTransform::Normal;
  state.primary = true;
  state.generation = kInitialInventoryGeneration;
  return state;
}

} // namespace

std::optional<DrmOutputInventory>
build_drm_output_inventory(const DeviceSnapshot &snapshot,
                           const DrmInventorySelection selection,
                           std::string &error) {
  error.clear();
  if (selection.connector_index >= snapshot.connectors.size()) {
    error = "selected DRM connector is outside the discovered inventory";
    return std::nullopt;
  }
  const auto &connector = snapshot.connectors[selection.connector_index];
  if (selection.mode_index >= connector.modes.size()) {
    error = "selected DRM mode is outside the connector inventory";
    return std::nullopt;
  }
  if (connector.status != ConnectionStatus::Connected) {
    error = "selected DRM connector is not connected";
    return std::nullopt;
  }
  if (connector.non_desktop) {
    error = "selected DRM connector is marked non-desktop";
    return std::nullopt;
  }
  if (connector.type == static_cast<std::uint32_t>(ConnectorType::Writeback)) {
    error = "selected DRM connector is a writeback connector";
    return std::nullopt;
  }

  DrmOutputInventory inventory;
  auto descriptor = build_descriptor(snapshot, connector, selection.mode_index,
                                     inventory.edid_participated, error);
  if (!descriptor)
    return std::nullopt;
  const auto selected_mode = descriptor->modes[selection.mode_index];
  const auto output_id = descriptor->id;
  inventory.layout.descriptors.emplace(output_id, std::move(*descriptor));
  inventory.layout.states.emplace(
      output_id,
      build_state(inventory.layout.descriptors.at(output_id), selected_mode));
  inventory.layout.primary_output_id = output_id;
  inventory.layout.root_logical_width = selected_mode.physical_width;
  inventory.layout.root_logical_height = selected_mode.physical_height;
  inventory.layout.generation = kInitialInventoryGeneration;
  inventory.layout.enabled_output_count = 1;
  inventory.layout.output_order.push_back(output_id);

  const auto validation = output::validate_layout(inventory.layout);
  if (!validation) {
    error = std::string("invalid DRM output inventory: ") +
            output::layout_validation_error_name(validation.error);
    return std::nullopt;
  }
  return inventory;
}

} // namespace glasswyrm::drm
