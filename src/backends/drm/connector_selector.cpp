#include "backends/drm/connector_selector.hpp"

#include "backends/drm/connector_name.hpp"

#include <algorithm>

namespace glasswyrm::drm {
namespace {

ConnectorSelectionStatus eligibility(const Connector& connector,
                                     const std::span<const Crtc> crtcs,
                                     const std::uint32_t width,
                                     const std::uint32_t height) {
  if (connector.status != ConnectionStatus::Connected)
    return ConnectorSelectionStatus::Disconnected;
  if (connector.modes.empty()) return ConnectorSelectionStatus::NoModes;
  if (connector.type == static_cast<std::uint32_t>(ConnectorType::Writeback))
    return ConnectorSelectionStatus::Writeback;
  if (connector.non_desktop) return ConnectorSelectionStatus::NonDesktop;
  const bool has_compatible_crtc =
      std::any_of(crtcs.begin(), crtcs.end(), [&connector](const Crtc& crtc) {
        return crtc.index < 32 &&
               (connector.possible_crtc_mask & (1U << crtc.index)) != 0;
      });
  if (!has_compatible_crtc)
    return ConnectorSelectionStatus::NoCompatibleCrtc;
  if (std::none_of(connector.modes.begin(), connector.modes.end(),
                   [width, height](const Mode& mode) {
                     return mode.width == width && mode.height == height;
                   }))
    return ConnectorSelectionStatus::NoMatchingMode;
  if (connector.current_crtc_id != 0) {
    const auto current = std::find_if(
        crtcs.begin(), crtcs.end(), [&connector](const Crtc& crtc) {
          return crtc.id == connector.current_crtc_id;
        });
    if (current != crtcs.end() && current->connector_ids.size() > 1)
      return ConnectorSelectionStatus::ClonedCrtc;
  }
  return ConnectorSelectionStatus::Success;
}

}  // namespace

ConnectorSelection select_connector(
    const std::span<const Connector> connectors,
    const std::span<const Crtc> crtcs, const std::uint32_t width,
    const std::uint32_t height,
    const std::optional<std::string_view> explicit_name) {
  if (explicit_name) {
    for (std::size_t index = 0; index < connectors.size(); ++index) {
      const auto& connector = connectors[index];
      if (connector_name(connector.type, connector.type_id) != *explicit_name)
        continue;
      return {eligibility(connector, crtcs, width, height), index};
    }
    return {ConnectorSelectionStatus::NotFound, 0};
  }

  std::optional<std::size_t> selected;
  for (std::size_t index = 0; index < connectors.size(); ++index) {
    if (eligibility(connectors[index], crtcs, width, height) !=
        ConnectorSelectionStatus::Success)
      continue;
    if (selected) return {ConnectorSelectionStatus::Ambiguous, *selected};
    selected = index;
  }
  return selected ? ConnectorSelection{ConnectorSelectionStatus::Success,
                                       *selected}
                  : ConnectorSelection{};
}

}  // namespace glasswyrm::drm
