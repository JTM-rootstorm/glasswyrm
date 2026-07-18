#include "glasswyrmd/composite_state.hpp"

#include <functional>
#include <new>
#include <utility>

namespace glasswyrm::server {

std::size_t CompositeRedirectKeyHash::operator()(
    const CompositeRedirectKey& key) const noexcept {
  const auto window = std::hash<std::uint32_t>{}(key.window);
  const auto scope = std::hash<std::uint8_t>{}(
      static_cast<std::uint8_t>(key.scope));
  return window ^ (scope + static_cast<std::size_t>(0x9E3779B9U) +
                   (window << 6U) + (window >> 2U));
}

bool CompositeState::valid_mode(const CompositeRedirectMode mode) noexcept {
  return mode == CompositeRedirectMode::Automatic ||
         mode == CompositeRedirectMode::Manual;
}

bool CompositeState::valid_scope(const CompositeRedirectScope scope) noexcept {
  return scope == CompositeRedirectScope::Direct ||
         scope == CompositeRedirectScope::Subtree;
}

CompositeState::RedirectMap::const_iterator CompositeState::find(
    const std::uint32_t window,
    const CompositeRedirectScope scope) const noexcept {
  return redirects_.find({window, scope});
}

CompositeRedirectStatus CompositeState::redirect(
    const ClientId client, const std::uint32_t window,
    const CompositeRedirectScope scope, const CompositeRedirectMode mode) {
  if (window == 0) return CompositeRedirectStatus::InvalidWindow;
  if (!valid_scope(scope)) return CompositeRedirectStatus::InvalidScope;
  if (!valid_mode(mode)) return CompositeRedirectStatus::InvalidMode;

  const CompositeRedirectKey key{window, scope};
  auto found = redirects_.find(key);
  if (mode == CompositeRedirectMode::Manual && found != redirects_.end() &&
      found->second.manual && *found->second.manual != client)
    return CompositeRedirectStatus::ManualConflict;

  try {
    if (found == redirects_.end()) {
      RedirectOwners owners;
      if (mode == CompositeRedirectMode::Manual)
        owners.manual = client;
      else
        owners.automatic.insert(client);
      redirects_.emplace(key, std::move(owners));
    } else if (mode == CompositeRedirectMode::Manual) {
      found->second.manual = client;
    } else {
      found->second.automatic.insert(client);
    }
    return CompositeRedirectStatus::Success;
  } catch (const std::bad_alloc&) {
    return CompositeRedirectStatus::BadAlloc;
  }
}

CompositeRedirectStatus CompositeState::unredirect(
    const ClientId client, const std::uint32_t window,
    const CompositeRedirectScope scope,
    const CompositeRedirectMode mode) noexcept {
  if (window == 0) return CompositeRedirectStatus::InvalidWindow;
  if (!valid_scope(scope)) return CompositeRedirectStatus::InvalidScope;
  if (!valid_mode(mode)) return CompositeRedirectStatus::InvalidMode;
  const auto key = CompositeRedirectKey{window, scope};
  const auto found = redirects_.find(key);
  if (found == redirects_.end())
    return CompositeRedirectStatus::NotRedirected;

  if (mode == CompositeRedirectMode::Manual) {
    if (!found->second.manual)
      return CompositeRedirectStatus::NotRedirected;
    if (*found->second.manual != client)
      return CompositeRedirectStatus::NotOwner;
    found->second.manual.reset();
  } else {
    if (!found->second.automatic.contains(client))
      return found->second.automatic.empty()
                 ? CompositeRedirectStatus::NotRedirected
                 : CompositeRedirectStatus::NotOwner;
    found->second.automatic.erase(client);
  }
  erase_if_empty(found);
  return CompositeRedirectStatus::Success;
}

bool CompositeState::redirected(
    const std::uint32_t window,
    const CompositeRedirectScope scope) const noexcept {
  return find(window, scope) != redirects_.end();
}

bool CompositeState::owns(const ClientId client, const std::uint32_t window,
                          const CompositeRedirectScope scope,
                          const CompositeRedirectMode mode) const noexcept {
  if (!valid_scope(scope) || !valid_mode(mode)) return false;
  const auto found = find(window, scope);
  if (found == redirects_.end()) return false;
  return mode == CompositeRedirectMode::Manual
             ? found->second.manual == client
             : found->second.automatic.contains(client);
}

std::optional<CompositeState::ClientId> CompositeState::manual_owner(
    const std::uint32_t window,
    const CompositeRedirectScope scope) const noexcept {
  const auto found = find(window, scope);
  return found == redirects_.end() ? std::nullopt : found->second.manual;
}

std::size_t CompositeState::automatic_owner_count(
    const std::uint32_t window,
    const CompositeRedirectScope scope) const noexcept {
  const auto found = find(window, scope);
  return found == redirects_.end() ? 0 : found->second.automatic.size();
}

std::size_t CompositeState::remove_client(const ClientId client) noexcept {
  std::size_t removed = 0;
  for (auto found = redirects_.begin(); found != redirects_.end();) {
    if (found->second.manual == client) {
      found->second.manual.reset();
      ++removed;
    }
    removed += found->second.automatic.erase(client);
    if (!found->second.manual && found->second.automatic.empty())
      found = redirects_.erase(found);
    else
      ++found;
  }
  return removed;
}

std::size_t CompositeState::remove_window(const std::uint32_t window) noexcept {
  std::size_t removed = 0;
  for (auto found = redirects_.begin(); found != redirects_.end();) {
    if (found->first.window == window) {
      found = redirects_.erase(found);
      ++removed;
    } else {
      ++found;
    }
  }
  return removed;
}

void CompositeState::erase_if_empty(const RedirectMap::iterator found) noexcept {
  if (!found->second.manual && found->second.automatic.empty())
    redirects_.erase(found);
}

}  // namespace glasswyrm::server
