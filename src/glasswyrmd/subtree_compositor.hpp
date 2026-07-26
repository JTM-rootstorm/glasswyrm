#pragma once

#include "glasswyrmd/pixel_storage.hpp"
#include "glasswyrmd/resource_table.hpp"

#include <cstdint>
#include <optional>

namespace glasswyrm::server {

// Returns the canonical opaque storage when no mapped InputOutput child needs
// to be composited over this direct-root window.
[[nodiscard]] const PixelStorage* direct_top_level_storage(
    const ResourceTable& resources, std::uint32_t top_level_xid) noexcept;

// Builds the opaque compositor-facing image for one direct-root window.
[[nodiscard]] std::optional<PixelStorage> compose_top_level_subtree(
    const ResourceTable& resources, std::uint32_t top_level_xid);

}  // namespace glasswyrm::server
