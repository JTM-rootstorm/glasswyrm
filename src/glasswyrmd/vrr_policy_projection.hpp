#pragma once

#include "glasswyrmd/lifecycle_types.hpp"
#include "glasswyrmd/vrr_state_cache.hpp"
#include "glasswyrmd/vrr_window_state.hpp"
#include "output/model/types.hpp"

#include <cstdint>
#include <map>
#include <vector>

namespace glasswyrm::server {

struct VrrPolicyProjection {
  std::vector<gwipc_policy_output_vrr_upsert> outputs;
  std::vector<gwipc_policy_window_vrr_upsert> windows;
  std::map<std::uint32_t, std::vector<std::uint64_t>> memberships;
};

void synchronize_vrr_windows(const LifecycleSnapshot& snapshot,
                             const VrrWindowStateStore& published,
                             VrrStateCache& cache);

[[nodiscard]] VrrPolicyProjection project_vrr_policy(
    const LifecycleSnapshot& snapshot, const output::OutputLayout& layout,
    const VrrStateCache& cache);

[[nodiscard]] std::vector<gwipc_surface_vrr_state> project_vrr_surfaces(
    const LifecycleSnapshot& snapshot, const VrrStateCache& cache,
    std::uint64_t policy_generation);

[[nodiscard]] std::uint64_t canonical_vrr_policy_hash(
    std::uint64_t base_policy_hash, const VrrPolicyProjection& input,
    const std::vector<gwipc_policy_output_vrr_state>& outputs,
    const std::vector<gwipc_policy_window_vrr_state>& windows) noexcept;

[[nodiscard]] bool validate_vrr_policy_result(
    const VrrPolicyProjection& input,
    const std::vector<gwipc_policy_output_vrr_state>& outputs,
    const std::vector<gwipc_policy_window_vrr_state>& windows) noexcept;

}  // namespace glasswyrm::server
