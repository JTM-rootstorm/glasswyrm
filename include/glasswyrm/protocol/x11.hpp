#pragma once

#include <cstdint>
#include <string_view>

namespace glasswyrm::protocol {

enum class ByteOrder : std::uint8_t {
  LsbFirst = 'l',
  MsbFirst = 'B',
};

enum class CompatibilityTier : std::uint8_t {
  ToyClients = 0,
  CoreHandshake = 1,
  SimpleClients = 2,
  XtermAndWindowManager = 3,
  SimpleGames = 4,
  Toolkits = 5,
  BrowsersWineProton = 6,
  DailyDriver = 7,
};

struct ProtocolIdentity {
  std::string_view name;
  CompatibilityTier tier;
  bool core_x11_enabled;
  bool tcp_listening_enabled;
};

bool is_valid_setup_byte_order(std::uint8_t value) noexcept;
std::string_view compatibility_tier_name(CompatibilityTier tier) noexcept;
ProtocolIdentity protocol_identity() noexcept;

}  // namespace glasswyrm::protocol
