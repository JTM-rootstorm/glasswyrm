#pragma once

#include "glasswyrmd/lifecycle_types.hpp"
#include "glasswyrmd/resource_table.hpp"

#include <span>

namespace glasswyrm::server {

[[nodiscard]] LifecycleSnapshot build_lifecycle_snapshot(
    const ResourceTable& resources, std::uint32_t focused_window);
[[nodiscard]] bool apply_policy_state(ResourceTable& resources,
                                      std::span<const AppliedPolicyWindow> policy,
                                      std::uint32_t& focused_window);

}  // namespace glasswyrm::server
