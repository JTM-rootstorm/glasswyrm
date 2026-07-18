#pragma once

#include "glasswyrmd/compositor_peer.hpp"

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

void promote(const CompositorSnapshotSubmission& pending,
             CompositorSnapshotSubmission& replay);

} // namespace glasswyrm::server::compositor_buffer_replay
