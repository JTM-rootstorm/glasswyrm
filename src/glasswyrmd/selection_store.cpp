#include "glasswyrmd/selection_store.hpp"

#include <algorithm>
#include <limits>
#include <new>

namespace glasswyrm::server {

bool SelectionStore::later(const std::uint32_t lhs,
                           const std::uint32_t rhs) noexcept {
  return static_cast<std::int32_t>(lhs - rhs) > 0;
}

SelectionOwnershipChange SelectionStore::set_owner(
    const std::uint64_t client, const std::uint32_t selection,
    const std::uint32_t owner_window, const bool owner_window_exists,
    const std::uint32_t request_time, const std::uint32_t server_time) {
  SelectionOwnershipChange result;
  result.effective_time = request_time == 0 ? server_time : request_time;
  if (selection == 0) {
    result.status = SelectionOwnershipStatus::InvalidSelection;
    return result;
  }
  if (owner_window != 0 && !owner_window_exists) {
    result.status = SelectionOwnershipStatus::InvalidOwnerWindow;
    return result;
  }
  const auto current = owners_.find(selection);
  const auto last_change =
      current == owners_.end() ? std::uint32_t{0}
                               : current->second.last_change_time;
  if ((last_change != 0 && later(last_change, result.effective_time)) ||
      later(result.effective_time, server_time)) {
    result.status = SelectionOwnershipStatus::IgnoredStaleTime;
    return result;
  }
  if (current != owners_.end()) result.previous_owner = current->second;
  if (owner_window == 0) {
    owners_.erase(selection);
  } else {
    owners_.insert_or_assign(
        selection, SelectionOwner{client, owner_window, result.effective_time});
  }
  return result;
}

std::optional<SelectionOwner> SelectionStore::owner(
    const std::uint32_t selection) const noexcept {
  const auto found = owners_.find(selection);
  return found == owners_.end() ? std::nullopt
                                : std::optional<SelectionOwner>(found->second);
}

SelectionConversion SelectionStore::convert(
    const std::uint64_t requestor_client,
    const std::uint32_t requestor_window, const std::uint32_t selection,
    const std::uint32_t target, const std::uint32_t property,
    const std::uint32_t request_time,
    const std::uint32_t server_time) const noexcept {
  SelectionConversion result;
  result.requestor_client = requestor_client;
  result.requestor_window = requestor_window;
  result.selection = selection;
  result.target = target;
  result.property = property;
  result.time = request_time == 0 ? server_time : request_time;
  result.owner = owner(selection);
  result.kind = result.owner ? SelectionConversionKind::ForwardToOwner
                             : SelectionConversionKind::NotifyNoOwner;
  if (!result.owner) result.property = 0;
  return result;
}

std::vector<std::uint32_t> SelectionStore::clear_window(
    const std::uint32_t window) noexcept {
  std::vector<std::uint32_t> cleared;
  for (auto owner = owners_.begin(); owner != owners_.end();) {
    if (owner->second.window != window) {
      ++owner;
      continue;
    }
    cleared.push_back(owner->first);
    owner = owners_.erase(owner);
  }
  std::ranges::sort(cleared);
  std::erase_if(xfixes_subscriptions_, [window](const auto& subscription) {
    return subscription.window == window;
  });
  return cleared;
}

std::vector<std::uint32_t> SelectionStore::clear_client(
    const std::uint64_t client) noexcept {
  std::vector<std::uint32_t> cleared;
  for (auto owner = owners_.begin(); owner != owners_.end();) {
    if (owner->second.client != client) {
      ++owner;
      continue;
    }
    cleared.push_back(owner->first);
    owner = owners_.erase(owner);
  }
  std::ranges::sort(cleared);
  std::erase_if(xfixes_subscriptions_, [client](const auto& subscription) {
    return subscription.client == client;
  });
  return cleared;
}

bool SelectionStore::select_xfixes(const std::uint64_t client,
                                   const std::uint32_t window,
                                   const std::uint32_t selection,
                                   const std::uint32_t mask) {
  const auto found = std::ranges::find_if(
      xfixes_subscriptions_, [=](const auto& subscription) {
        return subscription.client == client &&
               subscription.window == window &&
               subscription.selection == selection;
      });
  if (mask == 0) {
    if (found != xfixes_subscriptions_.end()) xfixes_subscriptions_.erase(found);
    return true;
  }
  if (found != xfixes_subscriptions_.end()) {
    found->mask = mask;
    return true;
  }
  if (xfixes_subscriptions_.size() >= 4096) return false;
  try {
    xfixes_subscriptions_.push_back({client, window, selection, mask});
  } catch (const std::bad_alloc&) {
    return false;
  }
  return true;
}

std::vector<XFixesSelectionNotification> SelectionStore::xfixes_notifications(
    const std::uint32_t selection, const std::uint8_t subtype,
    const std::uint32_t owner, const std::uint32_t timestamp,
    const std::uint32_t selection_timestamp) const {
  std::vector<XFixesSelectionNotification> result;
  const auto bit = std::uint32_t{1} << subtype;
  for (const auto& subscription : xfixes_subscriptions_)
    if (subscription.selection == selection && (subscription.mask & bit) != 0)
      result.push_back({subscription.client, subtype, subscription.window,
                        owner, selection, timestamp, selection_timestamp});
  std::ranges::sort(result, {}, &XFixesSelectionNotification::client);
  return result;
}

std::vector<std::pair<std::uint32_t, SelectionOwner>>
SelectionStore::owned_by_client(const std::uint64_t client) const {
  std::vector<std::pair<std::uint32_t, SelectionOwner>> result;
  for (const auto& entry : owners_)
    if (entry.second.client == client) result.push_back(entry);
  std::ranges::sort(result, {}, &std::pair<std::uint32_t, SelectionOwner>::first);
  return result;
}

std::vector<std::pair<std::uint32_t, SelectionOwner>>
SelectionStore::owned_by_window(const std::uint32_t window) const {
  std::vector<std::pair<std::uint32_t, SelectionOwner>> result;
  for (const auto& entry : owners_)
    if (entry.second.window == window) result.push_back(entry);
  std::ranges::sort(result, {}, &std::pair<std::uint32_t, SelectionOwner>::first);
  return result;
}

}  // namespace glasswyrm::server
