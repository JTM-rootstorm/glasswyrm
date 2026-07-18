#pragma once

#include <cstdint>

namespace glasswyrm::server {

class ServerState;

inline constexpr std::uint32_t kEwmhSupportingWindow = 4U;
inline constexpr std::uint32_t kPolicyWindowFlagAbove = 1U << 0;
inline constexpr std::uint32_t kPolicyWindowFlagBypassCompositor = 1U << 1;

[[nodiscard]] bool initialize_ewmh(ServerState& state);
void synchronize_ewmh_root_properties(ServerState& state);
[[nodiscard]] bool ewmh_property_is_protected(
    const ServerState& state, std::uint32_t window, std::uint32_t atom);
[[nodiscard]] bool ewmh_property_affects_policy(
    const ServerState& state, std::uint32_t atom);
[[nodiscard]] bool interpret_ewmh_window(ServerState& state,
                                         std::uint32_t window);

}  // namespace glasswyrm::server
