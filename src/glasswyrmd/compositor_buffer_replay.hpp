#pragma once

#include "glasswyrmd/compositor_peer.hpp"

#include <set>
#include <string>
#include <vector>

namespace glasswyrm::server::compositor_buffer_replay {

[[nodiscard]] bool prepare(CompositorSnapshotSubmission& replay,
                           std::string& error);

[[nodiscard]] bool rearm_snapshot(
    const CompositorSnapshotSubmission& submission,
    const CompositorSnapshotSubmission& replay, std::string& error);

[[nodiscard]] bool rearm_content(
    const std::vector<CompositorSnapshotSubmission::Damage>& damages,
    const CompositorSnapshotSubmission& replay, std::string& error);

[[nodiscard]] std::set<std::uint64_t> retired_buffer_ids(
    const CompositorSnapshotSubmission& submission,
    const CompositorSnapshotSubmission& replay);
[[nodiscard]] std::set<std::uint64_t> retired_buffer_ids(
    const CompositorContentSubmission& submission,
    const CompositorSnapshotSubmission& replay);

void promote(const CompositorSnapshotSubmission& pending,
             CompositorSnapshotSubmission& replay);
void promote_content(const CompositorContentSubmission& pending,
                     CompositorSnapshotSubmission& replay);

} // namespace glasswyrm::server::compositor_buffer_replay
