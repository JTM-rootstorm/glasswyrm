#pragma once

#include "protocol/x11/screen_model.hpp"

#include <cstdint>

namespace glasswyrm::server {

using ScreenModel = gw::protocol::x11::ScreenModel;
inline constexpr const ScreenModel& kScreenModel =
    gw::protocol::x11::kScreenModel;
// Reserved server-owned XIDs for extension-owned resources such as RENDER
// picture formats. Keep this outside every client resource-ID range.
inline constexpr std::uint32_t kServerExtensionResourceFirst = 0x1FFFF100U;
inline constexpr std::uint32_t kServerExtensionResourceLast = 0x1FFFF1FFU;

[[nodiscard]] constexpr bool resource_id_matches_client(
    const std::uint32_t xid, const std::uint32_t resource_base,
    const std::uint32_t resource_mask) noexcept {
  return xid != 0 &&
         (resource_base & resource_mask) == 0 &&
         (xid & ~resource_mask) == resource_base;
}

[[nodiscard]] constexpr bool is_server_owned_id(
    const std::uint32_t xid, const ScreenModel& screen = kScreenModel) noexcept {
  return xid == screen.root_window || xid == screen.default_colormap ||
         xid == screen.root_visual ||
         (xid >= kServerExtensionResourceFirst &&
          xid <= kServerExtensionResourceLast);
}

}  // namespace glasswyrm::server
