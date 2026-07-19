#pragma once

#include <cstdint>

namespace glasswyrm::output::vrr {

enum class PolicyMode : std::uint8_t {
  Off = 1,
  Fullscreen,
  Focused,
  AppRequested,
  AlwaysEligible,
};

enum class WindowPreference : std::uint8_t {
  Default = 0,
  Disable,
  Allow,
  Prefer,
};

enum class Decision : std::uint8_t {
  Disabled = 1,
  Enabled,
  Unsupported,
  Rejected,
};

[[nodiscard]] bool valid_policy_mode(PolicyMode mode) noexcept;
[[nodiscard]] bool
valid_window_preference(WindowPreference preference) noexcept;

} // namespace glasswyrm::output::vrr
