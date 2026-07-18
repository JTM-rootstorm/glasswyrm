#include "glasswyrmd/compositor_buffer_replay.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <map>
#include <set>
#include <unistd.h>

namespace glasswyrm::server::compositor_buffer_replay {
namespace {
bool normalize_readiness(const int descriptor, std::string& error) noexcept {
  std::uint64_t existing = 0;
  ssize_t count = -1;
  do {
    count = ::read(descriptor, &existing, sizeof(existing));
  } while (count < 0 && errno == EINTR);
  if (count >= 0 &&
      (count != static_cast<ssize_t>(sizeof(existing)) || existing != 1)) {
    error = "compositor buffer has an invalid readiness token";
    return false;
  }
  if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
    error = "compositor buffer readiness could not be inspected";
    return false;
  }

  std::atomic_thread_fence(std::memory_order_release);
  const std::uint64_t token = 1;
  do {
    count = ::write(descriptor, &token, sizeof(token));
  } while (count < 0 && errno == EINTR);
  if (count != static_cast<ssize_t>(sizeof(token))) {
    error = "compositor buffer readiness could not be rearmed";
    return false;
  }
  return true;
}
} // namespace

bool prepare(CompositorSnapshotSubmission& replay, std::string& error) {
  for (const auto& buffer : replay.buffers) {
    if (buffer.attach.synchronization == GWIPC_SYNCHRONIZATION_NONE)
      continue;
    const int descriptor = buffer.synchronization_fd;
    if (buffer.attach.synchronization != GWIPC_SYNCHRONIZATION_EVENTFD ||
        descriptor < 0) {
      error = "retained compositor buffer has invalid synchronization metadata";
      return false;
    }
    const auto damaged = std::ranges::find_if(
        replay.damages, [&](const auto& damage) {
          return damage.surface_id == buffer.attach.surface_id;
        });
    if (damaged == replay.damages.end()) {
      CompositorSnapshotSubmission::Damage damage;
      damage.surface_id = buffer.attach.surface_id;
      damage.rectangles.push_back(
          {0, 0, buffer.attach.width, buffer.attach.height});
      replay.damages.push_back(std::move(damage));
    }
  }
  error.clear();
  return true;
}

bool rearm_snapshot(const CompositorSnapshotSubmission& submission,
                    const CompositorSnapshotSubmission& replay,
                    std::string& error) {
  std::set<int> rearmed;
  for (const auto& damage : submission.damages) {
    auto found = std::ranges::find_if(
        submission.buffers, [&](const auto& buffer) {
          return buffer.attach.surface_id == damage.surface_id;
        });
    const CompositorSnapshotSubmission::Buffer* buffer =
        found != submission.buffers.end() ? &*found : nullptr;
    if (!buffer) {
      const auto retained = std::ranges::find_if(
          replay.buffers, [&](const auto& candidate) {
            return candidate.attach.surface_id == damage.surface_id;
          });
      if (retained != replay.buffers.end())
        buffer = &*retained;
    }
    if (!buffer) {
      error = "compositor damage has no submitted or retained buffer attachment";
      return false;
    }
    if (buffer->attach.synchronization == GWIPC_SYNCHRONIZATION_NONE)
      continue;
    const int descriptor = buffer->synchronization_fd;
    if (buffer->attach.synchronization != GWIPC_SYNCHRONIZATION_EVENTFD ||
        descriptor < 0) {
      error = "compositor buffer has invalid synchronization metadata";
      return false;
    }
    if (rearmed.insert(descriptor).second &&
        !normalize_readiness(descriptor, error))
      return false;
  }
  error.clear();
  return true;
}

bool rearm_content(
    const std::vector<CompositorSnapshotSubmission::Damage>& damages,
    const CompositorSnapshotSubmission& replay, std::string& error) {
  std::set<int> rearmed;
  for (const auto& damage : damages) {
    const auto found = std::ranges::find_if(
        replay.buffers, [&](const auto& buffer) {
          return buffer.attach.surface_id == damage.surface_id;
        });
    if (found == replay.buffers.end()) {
      error = "incremental compositor damage has no retained buffer attachment";
      return false;
    }
    const auto& buffer = *found;
    if (buffer.attach.synchronization == GWIPC_SYNCHRONIZATION_NONE)
      continue;
    const int descriptor = buffer.synchronization_fd;
    if (buffer.attach.synchronization != GWIPC_SYNCHRONIZATION_EVENTFD ||
        descriptor < 0) {
      error = "retained compositor buffer has invalid synchronization metadata";
      return false;
    }
    if (rearmed.insert(descriptor).second &&
        !normalize_readiness(descriptor, error))
      return false;
  }
  error.clear();
  return true;
}

void promote(const CompositorSnapshotSubmission& pending,
             CompositorSnapshotSubmission& replay) {
  CompositorSnapshotSubmission next = pending;
  next.buffers.clear();
  // Damage is a one-shot publication instruction, not retained scene state.
  // A future reconnect synthesizes fresh damage for synchronized attachments
  // after rearming them; retaining consumed damage would make an unrelated
  // cursor-only snapshot wait on content buffers that were not republished.
  next.damages.clear();
  std::map<std::uint64_t, CompositorSnapshotSubmission::Buffer> attachments;
  const std::set<std::uint64_t> retained_surfaces = [&] {
    std::set<std::uint64_t> ids;
    for (const auto& surface : pending.surfaces) ids.insert(surface.surface_id);
    return ids;
  }();
  for (const auto& buffer : replay.buffers)
    if (retained_surfaces.contains(buffer.attach.surface_id))
      attachments[buffer.attach.surface_id] = buffer;
  for (const auto& buffer : pending.buffers)
    attachments[buffer.attach.surface_id] = buffer;
  next.buffers.reserve(attachments.size());
  for (const auto& surface : pending.surfaces) {
    const auto found = attachments.find(surface.surface_id);
    if (found != attachments.end()) next.buffers.push_back(found->second);
  }
  replay = std::move(next);
}

} // namespace glasswyrm::server::compositor_buffer_replay
