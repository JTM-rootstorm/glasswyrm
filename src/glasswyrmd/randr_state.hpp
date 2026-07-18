#pragma once

#include "glasswyrmd/resource_table.hpp"

#include <cstdint>
#include <vector>

namespace glasswyrm::server {

inline constexpr std::uint32_t kRandROutputId = 0x00000100U;
inline constexpr std::uint32_t kRandRCrtcId = 0x00000101U;
inline constexpr std::uint32_t kRandRModeId = 0x00000102U;
inline constexpr std::uint32_t kRandRConfigurationTimestamp = 1U;
inline constexpr std::uint16_t kRandRRotate0 = 1U;
inline constexpr std::uint16_t kRandRSupportedNotifyMask = 0x000fU;

struct RandRSelection {
  ClientId client{};
  std::uint32_t window{};
  std::uint16_t mask{};
};

class RandRState {
 public:
  [[nodiscard]] bool select(ClientId client, std::uint32_t window,
                            std::uint16_t mask);
  [[nodiscard]] std::uint16_t selection(ClientId client,
                                        std::uint32_t window) const noexcept;
  [[nodiscard]] const std::vector<RandRSelection>& selections() const noexcept {
    return selections_;
  }
  [[nodiscard]] std::size_t clear_client(ClientId client) noexcept;
  [[nodiscard]] std::size_t clear_window(std::uint32_t window) noexcept;
  [[nodiscard]] std::size_t prune_windows(
      const ResourceTable& resources) noexcept;

 private:
  std::vector<RandRSelection> selections_;
};

}  // namespace glasswyrm::server
