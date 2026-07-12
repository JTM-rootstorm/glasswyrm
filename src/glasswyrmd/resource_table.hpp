#pragma once

#include "glasswyrmd/resource_id.hpp"
#include "glasswyrmd/window.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

namespace glasswyrm::server {

using ClientId = std::uint64_t;

enum class ResourceType { Window };

struct ResourceRecord {
  ResourceType type{ResourceType::Window};
  std::optional<ClientId> owner;
  std::variant<WindowResource> payload;
};

enum class CreateWindowStatus {
  Success,
  BadIdChoice,
  BadWindow,
  BadValue,
  BadMatch,
  BadAlloc,
};

enum class DestroyWindowStatus { Success, BadWindow, RootPreserved };
enum class PropertyMutationStatus {
  Success,
  BadWindow,
  BadMatch,
  BadAlloc,
};
enum class PropertyReadStatus { Success, BadWindow, BadValue };

struct CleanupResult {
  std::size_t resources_destroyed{0};
  std::size_t property_bytes_released{0};
};

struct PropertyReadResult {
  PropertyReadStatus status{PropertyReadStatus::Success};
  bool present{false};
  bool type_matched{false};
  bool deleted{false};
  PropertySlice value;
};

inline constexpr std::size_t kMaximumBytesPerProperty = 4U * 1024U * 1024U;
inline constexpr std::size_t kMaximumTotalPropertyBytes = 64U * 1024U * 1024U;
inline constexpr std::size_t kMaximumPropertiesPerWindow = 4096;

struct ResourceLimits {
  std::size_t maximum_bytes_per_property{kMaximumBytesPerProperty};
  std::size_t maximum_total_property_bytes{kMaximumTotalPropertyBytes};
  std::size_t maximum_properties_per_window{kMaximumPropertiesPerWindow};
};

class ResourceTable {
 public:
  explicit ResourceTable(ScreenModel screen = kScreenModel,
                         ResourceLimits limits = {});

  [[nodiscard]] const ScreenModel& screen() const noexcept { return screen_; }
  [[nodiscard]] const ResourceRecord* find(std::uint32_t xid) const noexcept;
  [[nodiscard]] ResourceRecord* find(std::uint32_t xid) noexcept;
  [[nodiscard]] const WindowResource* find_window(
      std::uint32_t xid) const noexcept;
  [[nodiscard]] WindowResource* find_window(std::uint32_t xid) noexcept;

  [[nodiscard]] bool valid_new_resource_id(std::uint32_t xid,
                                           std::uint32_t resource_base,
                                           std::uint32_t resource_mask) const;
  [[nodiscard]] CreateWindowStatus create_window(
      ClientId owner, std::uint32_t resource_base, std::uint32_t resource_mask,
      const WindowCreateSpec& spec);
  [[nodiscard]] DestroyWindowStatus destroy_window(std::uint32_t xid,
                                                   CleanupResult* result = nullptr);
  [[nodiscard]] CleanupResult cleanup_client(ClientId owner);
  [[nodiscard]] bool set_event_selection(std::uint32_t window, ClientId client,
                                         std::uint32_t mask);
  [[nodiscard]] std::uint32_t event_selection(std::uint32_t window,
                                              ClientId client) const noexcept;
  [[nodiscard]] std::uint32_t all_event_selections(
      std::uint32_t window) const noexcept;
  void remove_event_selections(ClientId client) noexcept;

  [[nodiscard]] PropertyMutationStatus change_property(
      std::uint32_t window, std::uint32_t property_atom, Property value,
      PropertyMode mode);
  [[nodiscard]] bool delete_property(std::uint32_t window,
                                     std::uint32_t property_atom);
  [[nodiscard]] PropertyReadResult get_property(
      std::uint32_t window, std::uint32_t property_atom,
      std::uint32_t requested_type, bool delete_after_read,
      std::uint32_t long_offset, std::uint32_t long_length);
  [[nodiscard]] std::vector<std::uint32_t> list_properties(
      std::uint32_t window) const;

  [[nodiscard]] std::size_t resource_count(ResourceType type) const noexcept;
  [[nodiscard]] std::size_t resource_count_by_owner(ClientId owner) const noexcept;
  [[nodiscard]] std::size_t total_property_bytes() const noexcept {
    return total_property_bytes_;
  }
  [[nodiscard]] bool invariants_hold() const noexcept;

 private:
  void destroy_window_tree(std::uint32_t xid, CleanupResult& result);
  void destroy_leaf(std::uint32_t xid, CleanupResult& result);

  ScreenModel screen_;
  ResourceLimits limits_;
  std::unordered_map<std::uint32_t, ResourceRecord> resources_;
  std::unordered_map<ClientId, std::vector<std::uint32_t>> resources_by_owner_;
  std::size_t total_property_bytes_{0};
};

}  // namespace glasswyrm::server
