#include <glasswyrm/protocol/x11.hpp>

namespace glasswyrm::protocol {

bool is_valid_setup_byte_order(std::uint8_t value) noexcept {
  return value == static_cast<std::uint8_t>(ByteOrder::LsbFirst) ||
         value == static_cast<std::uint8_t>(ByteOrder::MsbFirst);
}

std::string_view compatibility_tier_name(CompatibilityTier tier) noexcept {
  switch (tier) {
    case CompatibilityTier::ToyClients:
      return "tier-0-toy-clients";
    case CompatibilityTier::CoreHandshake:
      return "tier-1-core-handshake";
    case CompatibilityTier::SimpleClients:
      return "tier-2-simple-clients";
    case CompatibilityTier::XtermAndWindowManager:
      return "tier-3-xterm-window-manager";
    case CompatibilityTier::SimpleGames:
      return "tier-4-simple-games";
    case CompatibilityTier::Toolkits:
      return "tier-5-toolkits";
    case CompatibilityTier::BrowsersWineProton:
      return "tier-6-browsers-wine-proton";
    case CompatibilityTier::DailyDriver:
      return "tier-7-daily-driver";
  }
  return "unknown";
}

ProtocolIdentity protocol_identity() noexcept {
  return ProtocolIdentity{
      .name = "X11-compatible local protocol scaffold",
      .tier = CompatibilityTier::ToyClients,
      .core_x11_enabled = false,
      .tcp_listening_enabled = false,
  };
}

}  // namespace glasswyrm::protocol
