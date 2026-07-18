#include "glasswyrmd/randr_state.hpp"

#include <algorithm>

namespace glasswyrm::server {

bool RandRState::select(const ClientId client, const std::uint32_t window,
                        const std::uint16_t mask) {
  const auto matches = [client, window](const RandRSelection& selection) {
    return selection.client == client && selection.window == window;
  };
  const auto found = std::find_if(selections_.begin(), selections_.end(),
                                  matches);
  if (mask == 0) {
    if (found != selections_.end()) selections_.erase(found);
    return true;
  }
  if (found != selections_.end()) {
    found->mask = mask;
    return true;
  }
  try {
    selections_.push_back({client, window, mask});
    return true;
  } catch (...) {
    return false;
  }
}

std::uint16_t RandRState::selection(const ClientId client,
                                    const std::uint32_t window) const noexcept {
  const auto found = std::find_if(
      selections_.begin(), selections_.end(),
      [client, window](const RandRSelection& selection) {
        return selection.client == client && selection.window == window;
      });
  return found == selections_.end() ? 0 : found->mask;
}

std::size_t RandRState::clear_client(const ClientId client) noexcept {
  const auto before = selections_.size();
  std::erase_if(selections_, [client](const RandRSelection& selection) {
    return selection.client == client;
  });
  return before - selections_.size();
}

std::size_t RandRState::clear_window(const std::uint32_t window) noexcept {
  const auto before = selections_.size();
  std::erase_if(selections_, [window](const RandRSelection& selection) {
    return selection.window == window;
  });
  return before - selections_.size();
}

std::size_t RandRState::prune_windows(
    const ResourceTable& resources) noexcept {
  const auto before = selections_.size();
  std::erase_if(selections_, [&resources](const RandRSelection& selection) {
    return resources.find_window(selection.window) == nullptr;
  });
  return before - selections_.size();
}

}  // namespace glasswyrm::server
