#pragma once

#include "glasswyrmd/compositor_peer.hpp"
#include "glasswyrmd/lifecycle_types.hpp"
#include "glasswyrmd/policy_peer.hpp"
#include "glasswyrmd/vrr_state_cache.hpp"
#include "output/model/types.hpp"
#include "protocol/x11/screen_model.hpp"

#include <optional>

namespace glasswyrm::server {
[[nodiscard]] PolicySnapshotSubmission project_policy(
    const LifecycleSnapshot&, std::uint64_t commit, std::uint64_t generation,
    const output::OutputLayout* layout = nullptr,
    const VrrStateCache* vrr = nullptr);
[[nodiscard]] std::optional<LifecycleSnapshot> apply_policy_result(
    const LifecycleSnapshot&, const PolicySnapshotResult&,
    const output::OutputLayout* layout = nullptr,
    VrrStateCache* vrr = nullptr);
[[nodiscard]] bool policy_output_facts_match(
    const LifecycleSnapshot& policy_input,
    const LifecycleSnapshot& evaluated) noexcept;
[[nodiscard]] CompositorSnapshotSubmission project_compositor(
    const LifecycleSnapshot&, std::uint64_t commit, std::uint64_t generation,
    bool software_content = false,
    const output::OutputLayout* layout = nullptr,
    VrrStateCache* vrr = nullptr);
[[nodiscard]] std::vector<AppliedPolicyWindow> applied_policy(
    const LifecycleSnapshot&);
}  // namespace glasswyrm::server
