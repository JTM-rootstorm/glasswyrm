#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace glasswyrm::server {

enum class CompositeRedirectMode : std::uint8_t { Automatic = 0, Manual = 1 };
enum class CompositeRedirectScope : std::uint8_t { Direct, Subtree };

enum class CompositeRedirectStatus {
  Success,
  InvalidWindow,
  InvalidScope,
  InvalidMode,
  ManualConflict,
  NotRedirected,
  NotOwner,
  BadAlloc,
};

struct CompositeRedirectKey {
  std::uint32_t window{};
  CompositeRedirectScope scope{CompositeRedirectScope::Direct};

  friend bool operator==(const CompositeRedirectKey&,
                         const CompositeRedirectKey&) = default;
};

struct CompositeRedirectKeyHash {
  [[nodiscard]] std::size_t operator()(
      const CompositeRedirectKey& key) const noexcept;
};

class CompositeState {
 public:
  using ClientId = std::uint64_t;

  [[nodiscard]] CompositeRedirectStatus redirect(
      ClientId client, std::uint32_t window, CompositeRedirectScope scope,
      CompositeRedirectMode mode);
  [[nodiscard]] CompositeRedirectStatus unredirect(
      ClientId client, std::uint32_t window, CompositeRedirectScope scope,
      CompositeRedirectMode mode) noexcept;

  [[nodiscard]] bool redirected(std::uint32_t window,
                                CompositeRedirectScope scope) const noexcept;
  [[nodiscard]] bool owns(ClientId client, std::uint32_t window,
                          CompositeRedirectScope scope,
                          CompositeRedirectMode mode) const noexcept;
  [[nodiscard]] std::optional<ClientId> manual_owner(
      std::uint32_t window, CompositeRedirectScope scope) const noexcept;
  [[nodiscard]] std::size_t automatic_owner_count(
      std::uint32_t window, CompositeRedirectScope scope) const noexcept;
  [[nodiscard]] std::size_t entry_count() const noexcept {
    return redirects_.size();
  }

  std::size_t remove_client(ClientId client) noexcept;
  std::size_t remove_window(std::uint32_t window) noexcept;

 private:
  struct RedirectOwners {
    std::optional<ClientId> manual;
    std::unordered_set<ClientId> automatic;
  };

  using RedirectMap =
      std::unordered_map<CompositeRedirectKey, RedirectOwners,
                         CompositeRedirectKeyHash>;

  [[nodiscard]] static bool valid_mode(CompositeRedirectMode mode) noexcept;
  [[nodiscard]] static bool valid_scope(CompositeRedirectScope scope) noexcept;
  [[nodiscard]] RedirectMap::const_iterator find(
      std::uint32_t window, CompositeRedirectScope scope) const noexcept;
  void erase_if_empty(RedirectMap::iterator found) noexcept;

  RedirectMap redirects_;
};

}  // namespace glasswyrm::server
