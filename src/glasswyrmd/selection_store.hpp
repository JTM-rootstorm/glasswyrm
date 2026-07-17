#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace glasswyrm::server {

struct SelectionOwner {
  std::uint64_t client{0};
  std::uint32_t window{0};
  std::uint32_t last_change_time{0};

  [[nodiscard]] bool operator==(const SelectionOwner&) const noexcept = default;
};

enum class SelectionOwnershipStatus {
  Applied,
  IgnoredStaleTime,
  InvalidSelection,
  InvalidOwnerWindow,
};

struct SelectionOwnershipChange {
  SelectionOwnershipStatus status{SelectionOwnershipStatus::Applied};
  std::uint32_t effective_time{0};
  std::optional<SelectionOwner> previous_owner;
};

enum class SelectionConversionKind { NotifyNoOwner, ForwardToOwner };

struct SelectionConversion {
  SelectionConversionKind kind{SelectionConversionKind::NotifyNoOwner};
  std::uint64_t requestor_client{0};
  std::uint32_t requestor_window{0};
  std::uint32_t selection{0};
  std::uint32_t target{0};
  std::uint32_t property{0};
  std::uint32_t time{0};
  std::optional<SelectionOwner> owner;
};

class SelectionStore {
 public:
  [[nodiscard]] SelectionOwnershipChange set_owner(
      std::uint64_t client, std::uint32_t selection,
      std::uint32_t owner_window, bool owner_window_exists,
      std::uint32_t request_time, std::uint32_t server_time);
  [[nodiscard]] std::optional<SelectionOwner> owner(
      std::uint32_t selection) const noexcept;
  [[nodiscard]] SelectionConversion convert(
      std::uint64_t requestor_client, std::uint32_t requestor_window,
      std::uint32_t selection, std::uint32_t target, std::uint32_t property,
      std::uint32_t request_time, std::uint32_t server_time) const noexcept;
  [[nodiscard]] std::vector<std::uint32_t> clear_window(
      std::uint32_t window) noexcept;
  [[nodiscard]] std::vector<std::uint32_t> clear_client(
      std::uint64_t client) noexcept;
  [[nodiscard]] std::size_t size() const noexcept { return owners_.size(); }

 private:
  [[nodiscard]] static bool later(std::uint32_t lhs,
                                  std::uint32_t rhs) noexcept;

  std::unordered_map<std::uint32_t, SelectionOwner> owners_;
};

}  // namespace glasswyrm::server
