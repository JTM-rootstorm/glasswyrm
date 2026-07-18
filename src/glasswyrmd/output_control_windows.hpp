#pragma once

#include "glasswyrmd/lifecycle_types.hpp"
#include "gwcomp/output_inventory_publisher.hpp"
#include "output/model/layout.hpp"

#include <optional>
#include <vector>

namespace glasswyrm::server {

[[nodiscard]] std::optional<
    std::vector<compositor::OutputInventoryWindow>>
build_output_control_windows(const LifecycleSnapshot &snapshot,
                             const output::OutputLayout &layout);

} // namespace glasswyrm::server
