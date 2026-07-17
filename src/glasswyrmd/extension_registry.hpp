#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace glasswyrm::server {

enum class ExtensionKind {
  BigRequests,
  MitShm,
  XFixes,
  Damage,
  Render,
  Composite,
  RandR,
};

enum class ExtensionCapability { GameCompat };

struct ExtensionDescriptor {
  std::string_view name;
  ExtensionKind kind;
  ExtensionCapability capability;
  std::uint8_t major_opcode{};
  std::uint8_t first_event{};
  std::uint8_t event_count{};
  std::uint8_t first_error{};
  std::uint8_t error_count{};
  std::uint16_t maximum_major_version{};
  std::uint16_t maximum_minor_version{};
};

inline constexpr std::array<ExtensionDescriptor, 7> kExtensionRegistry{{
    {"BIG-REQUESTS", ExtensionKind::BigRequests,
     ExtensionCapability::GameCompat, 128, 0, 0, 0, 0, 1, 0},
    {"MIT-SHM", ExtensionKind::MitShm, ExtensionCapability::GameCompat, 129,
     64, 1, 128, 1, 1, 1},
    {"XFIXES", ExtensionKind::XFixes, ExtensionCapability::GameCompat, 130,
     65, 1, 129, 1, 2, 0},
    {"DAMAGE", ExtensionKind::Damage, ExtensionCapability::GameCompat, 131,
     66, 1, 130, 1, 1, 1},
    {"RENDER", ExtensionKind::Render, ExtensionCapability::GameCompat, 132,
     0, 0, 131, 5, 0, 11},
    {"COMPOSITE", ExtensionKind::Composite,
     ExtensionCapability::GameCompat, 133, 0, 0, 0, 0, 0, 4},
    {"RANDR", ExtensionKind::RandR, ExtensionCapability::GameCompat, 134, 67,
     2, 136, 3, 1, 3},
}};

[[nodiscard]] constexpr bool extension_ranges_are_valid() noexcept {
  for (std::size_t left = 0; left < kExtensionRegistry.size(); ++left) {
    const auto& a = kExtensionRegistry[left];
    if (left != 0 &&
        a.major_opcode <= kExtensionRegistry[left - 1].major_opcode)
      return false;
    for (std::size_t right = left + 1; right < kExtensionRegistry.size();
         ++right) {
      const auto& b = kExtensionRegistry[right];
      const bool events_overlap =
          a.event_count != 0 && b.event_count != 0 &&
          a.first_event < static_cast<unsigned>(b.first_event) + b.event_count &&
          b.first_event < static_cast<unsigned>(a.first_event) + a.event_count;
      const bool errors_overlap =
          a.error_count != 0 && b.error_count != 0 &&
          a.first_error < static_cast<unsigned>(b.first_error) + b.error_count &&
          b.first_error < static_cast<unsigned>(a.first_error) + a.error_count;
      if (events_overlap || errors_overlap) return false;
    }
  }
  return true;
}
static_assert(extension_ranges_are_valid());

[[nodiscard]] const ExtensionDescriptor*
find_extension(std::string_view name) noexcept;
[[nodiscard]] const ExtensionDescriptor*
find_extension(std::uint8_t major_opcode) noexcept;
[[nodiscard]] const ExtensionDescriptor*
find_extension(ExtensionKind kind) noexcept;
[[nodiscard]] bool known_extension_name(std::string_view name) noexcept;

class ExtensionRegistry {
 public:
  ExtensionRegistry() = default;
  ExtensionRegistry(bool enabled, std::span<const std::string> disabled);

  [[nodiscard]] bool profile_enabled() const noexcept { return enabled_; }
  [[nodiscard]] bool enabled(std::string_view name) const noexcept;
  [[nodiscard]] bool enabled(std::uint8_t major_opcode) const noexcept;
  [[nodiscard]] const ExtensionDescriptor*
  query(std::string_view name) const noexcept;
  [[nodiscard]] const ExtensionDescriptor*
  query(std::uint8_t major_opcode) const noexcept;
  [[nodiscard]] std::vector<std::string_view> enabled_names() const;

 private:
  bool enabled_{false};
  std::array<bool, kExtensionRegistry.size()> disabled_{};
};

}  // namespace glasswyrm::server
