#pragma once

#include "backends/drm/resources.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace glasswyrm::drm {

enum class ConnectorSelectionStatus {
  Success,
  NotFound,
  Disconnected,
  NoModes,
  Writeback,
  NonDesktop,
  NoCompatibleCrtc,
  NoMatchingMode,
  ClonedCrtc,
  NoEligibleConnector,
  Ambiguous,
};

struct ConnectorSelection {
  ConnectorSelectionStatus status{ConnectorSelectionStatus::NoEligibleConnector};
  std::size_t connector_index{};
};

[[nodiscard]] ConnectorSelection select_connector(
    std::span<const Connector> connectors, std::span<const Crtc> crtcs,
    std::uint32_t width, std::uint32_t height,
    std::optional<std::string_view> explicit_name = std::nullopt);

}  // namespace glasswyrm::drm
