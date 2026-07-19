#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace glasswyrm::server {

enum class WindowVrrPreference : std::uint16_t {
  Default = 0,
  Disable = 1,
  Allow = 2,
  Prefer = 3,
};

enum class OutputVrrPolicyMode : std::uint16_t {
  Off = 1,
  Fullscreen = 2,
  Focused = 3,
  AppRequested = 4,
  AlwaysEligible = 5,
};

inline constexpr std::uint32_t kVrrPreferenceChanged = UINT32_C(1) << 0U;
inline constexpr std::uint32_t kVrrEligibilityChanged = UINT32_C(1) << 1U;
inline constexpr std::uint32_t kVrrEffectiveStateChanged = UINT32_C(1) << 2U;
inline constexpr std::uint32_t kKnownVrrEventMask = UINT32_C(0x7);

struct WindowVrrState {
  WindowVrrPreference preference{WindowVrrPreference::Default};
  std::uint32_t primary_output{};
  bool policy_eligible{};
  bool selected_candidate{};
  bool effective_output_enabled{};
  std::uint64_t reason_flags{};
  std::uint64_t policy_generation{1};
  std::uint64_t output_state_generation{1};
  std::unordered_map<std::uint64_t, std::uint32_t> event_selections;
};

struct PublishedOutputVrrState {
  OutputVrrPolicyMode policy{OutputVrrPolicyMode::Off};
  bool connector_property_present{};
  bool hardware_capable{};
  bool kms_controllable{};
  bool simulated{};
  bool effective_enabled{};
  bool range_available{};
  std::uint32_t minimum_refresh_millihertz{};
  std::uint32_t maximum_refresh_millihertz{};
  std::uint32_t candidate_window{};
  std::uint64_t reason_flags{};
  std::uint64_t state_generation{1};
  std::uint64_t latest_interval_nanoseconds{};
};

struct VrrPreferenceChange {
  std::uint32_t window{};
  WindowVrrPreference preference{WindowVrrPreference::Default};
};

struct VrrNotification {
  std::uint64_t client{};
  std::vector<std::uint8_t> bytes;
};

class VrrWindowStateStore {
 public:
  [[nodiscard]] WindowVrrState* find_window(std::uint32_t window) noexcept;
  [[nodiscard]] const WindowVrrState* find_window(
      std::uint32_t window) const noexcept;
  [[nodiscard]] PublishedOutputVrrState* find_output(
      std::uint32_t output) noexcept;
  [[nodiscard]] const PublishedOutputVrrState* find_output(
      std::uint32_t output) const noexcept;

  WindowVrrState& ensure_window(std::uint32_t window);
  PublishedOutputVrrState& ensure_output(std::uint32_t output);
  void erase_window(std::uint32_t window) noexcept;

 private:
  std::unordered_map<std::uint32_t, WindowVrrState> windows_;
  std::unordered_map<std::uint32_t, PublishedOutputVrrState> outputs_;
};

[[nodiscard]] bool valid_vrr_preference(std::uint16_t value) noexcept;
[[nodiscard]] std::uint32_t vrr_change_mask(const WindowVrrState& before,
                                            const WindowVrrState& after) noexcept;

}  // namespace glasswyrm::server
