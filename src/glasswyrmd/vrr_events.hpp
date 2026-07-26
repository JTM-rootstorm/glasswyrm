#pragma once

#include "glasswyrmd/extensions/gw_vrr.hpp"
#include "glasswyrmd/vrr_state_cache.hpp"

#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace glasswyrm::server {

class ClientConnection;

struct VrrWindowTransition {
  std::uint32_t window_id{};
  WindowVrrState before;
  WindowVrrState after;
  OutputVrrPolicyMode output_policy{OutputVrrPolicyMode::Off};
};

struct VrrEventBatch {
  std::vector<VrrWindowTransition> windows;
  std::map<std::uint32_t, PublishedOutputVrrState> outputs;
};

[[nodiscard]] VrrSessionStateStatus synchronize_vrr_session_state(
    VrrStateCache& cache, VrrWindowStateStore& published,
    const std::map<std::uint64_t, std::uint32_t>& output_xids,
    gwipc_session_state state, VrrEventBatch& events);

[[nodiscard]] VrrEventBatch prepare_vrr_event_batch(
    const VrrStateCache& cache, const VrrWindowStateStore& published,
    const std::map<std::uint64_t, std::uint32_t>& output_xids);

void apply_vrr_event_batch(VrrWindowStateStore& published,
                           const VrrEventBatch& batch);

void enqueue_vrr_event_batch_notifications(
    const VrrWindowStateStore& published, const VrrEventBatch& batch,
    std::span<ClientConnection* const> recipients);

[[nodiscard]] std::vector<VrrNotification> publish_vrr_event_batch(
    VrrWindowStateStore& published, const VrrEventBatch& batch,
    gw::protocol::x11::ByteOrder order, std::uint64_t sequence);

}  // namespace glasswyrm::server
