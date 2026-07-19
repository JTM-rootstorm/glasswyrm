#pragma once

#include "m14_vrr_client_options.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace gw::test::m14 {

inline constexpr std::uint16_t kPatternWidth = 256;
inline constexpr std::uint16_t kPatternHeight = 192;
inline constexpr std::uint16_t kDamageWidth = 64;
inline constexpr std::uint16_t kDamageHeight = 64;
inline constexpr std::uint64_t kFinalSpinNanoseconds = 200'000;

struct ClientState {
  ClientMode mode{ClientMode::Windowed};
  std::uint32_t window{};
  std::uint16_t width{};
  std::uint16_t height{};
  bool prefer{};
  bool fullscreen_requested{};
  bool borderless{};
  std::uint32_t frame_count{};
  std::uint32_t target_refresh_hz{};
  std::uint64_t target_interval_nanoseconds{};
};

[[nodiscard]] std::uint64_t
target_interval_nanoseconds(std::uint32_t refresh_hz) noexcept;
[[nodiscard]] bool absolute_deadline(std::uint64_t start_nanoseconds,
                                     std::uint64_t interval_nanoseconds,
                                     std::uint32_t frame_index,
                                     std::uint64_t &deadline) noexcept;
[[nodiscard]] bool wait_until_monotonic(
    std::uint64_t deadline_nanoseconds,
    std::uint64_t final_spin_nanoseconds = kFinalSpinNanoseconds) noexcept;

[[nodiscard]] std::vector<std::uint32_t>
deterministic_pattern(std::uint16_t width, std::uint16_t height);
[[nodiscard]] std::vector<std::uint32_t>
deterministic_damage(std::uint32_t frame_index);
[[nodiscard]] std::string client_state_json(const ClientState &state);
void write_client_state(const std::string &path, const ClientState &state);

} // namespace gw::test::m14
