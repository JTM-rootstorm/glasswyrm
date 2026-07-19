#pragma once

#include "glasswyrmd/request_dispatcher.hpp"
#include "glasswyrmd/vrr_window_state.hpp"

#include <optional>

namespace glasswyrm::server::extensions {

inline constexpr std::uint8_t kGwVrrMajorOpcode = 136;
inline constexpr std::uint8_t kGwVrrEventBase = 70;
inline constexpr std::uint8_t kGwVrrBadPreference = 141;
inline constexpr std::uint8_t kGwVrrBadWindow = 142;

struct VrrDispatchResult {
  DispatchResult dispatch;
  std::optional<VrrPreferenceChange> preference_change;
};

[[nodiscard]] VrrDispatchResult dispatch_gw_vrr(
    ServerState& state, VrrWindowStateStore& vrr,
    const DispatchContext& context,
    const gw::protocol::x11::FramedRequest& request);

[[nodiscard]] std::vector<std::uint8_t> encode_gw_vrr_notify(
    gw::protocol::x11::ByteOrder order, std::uint64_t sequence,
    std::uint32_t change_mask, std::uint32_t window,
    const WindowVrrState& state, OutputVrrPolicyMode policy);

[[nodiscard]] std::vector<VrrNotification> gw_vrr_notifications(
    gw::protocol::x11::ByteOrder order, std::uint64_t sequence,
    std::uint32_t window, const WindowVrrState& before,
    const WindowVrrState& after, OutputVrrPolicyMode policy);

[[nodiscard]] std::vector<std::uint8_t> gw_vrr_lifecycle_completion(
    gw::protocol::x11::ByteOrder order, std::uint64_t sequence,
    const DeferredVrrMutation& mutation, bool accepted);

}  // namespace glasswyrm::server::extensions
