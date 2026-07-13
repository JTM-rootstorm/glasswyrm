#pragma once

#include "glasswyrmd/pixel_storage.hpp"
#include "glasswyrmd/resource_table.hpp"

#include <cstdint>
#include <optional>

namespace glasswyrm::server {

// Builds the opaque compositor-facing image for one direct-root window.
[[nodiscard]] std::optional<PixelStorage> compose_top_level_subtree(
    const ResourceTable& resources, std::uint32_t top_level_xid);

}  // namespace glasswyrm::server
