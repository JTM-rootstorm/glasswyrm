#include "backends/drm/connector_name.hpp"

#include "backends/drm/resources.hpp"

namespace glasswyrm::drm {

std::string_view connector_type_name(
    const std::uint32_t connector_type) noexcept {
  switch (static_cast<ConnectorType>(connector_type)) {
    case ConnectorType::Vga: return "VGA";
    case ConnectorType::DviI: return "DVI-I";
    case ConnectorType::DviD: return "DVI-D";
    case ConnectorType::DviA: return "DVI-A";
    case ConnectorType::Composite: return "Composite";
    case ConnectorType::SVideo: return "SVIDEO";
    case ConnectorType::Lvds: return "LVDS";
    case ConnectorType::Component: return "Component";
    case ConnectorType::Din9: return "DIN";
    case ConnectorType::DisplayPort: return "DP";
    case ConnectorType::HdmiA: return "HDMI-A";
    case ConnectorType::HdmiB: return "HDMI-B";
    case ConnectorType::Tv: return "TV";
    case ConnectorType::Edp: return "eDP";
    case ConnectorType::Virtual: return "Virtual";
    case ConnectorType::Dsi: return "DSI";
    case ConnectorType::Dpi: return "DPI";
    case ConnectorType::Writeback: return "Writeback";
    case ConnectorType::Spi: return "SPI";
    case ConnectorType::Usb: return "USB";
    case ConnectorType::Unknown: return {};
  }
  return {};
}

std::string_view connection_status_name(const ConnectionStatus status) noexcept {
  switch (status) {
    case ConnectionStatus::Connected: return "connected";
    case ConnectionStatus::Disconnected: return "disconnected";
    case ConnectionStatus::Unknown: return "unknown";
  }
  return "unknown";
}

std::string connector_name(const std::uint32_t connector_type,
                           const std::uint32_t connector_type_id) {
  const auto type_name = connector_type_name(connector_type);
  if (!type_name.empty())
    return std::string(type_name) + "-" + std::to_string(connector_type_id);
  return "Unknown-" + std::to_string(connector_type) + "-" +
         std::to_string(connector_type_id);
}

}  // namespace glasswyrm::drm
