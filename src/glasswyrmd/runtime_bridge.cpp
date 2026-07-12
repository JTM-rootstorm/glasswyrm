#include "glasswyrmd/runtime_bridge.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace glasswyrm::server {
namespace {
constexpr std::array kRetryDelays = {
    std::chrono::milliseconds(50),  std::chrono::milliseconds(100),
    std::chrono::milliseconds(200), std::chrono::milliseconds(400),
    std::chrono::milliseconds(800), std::chrono::milliseconds(1000)};
}

RuntimeBridge::RuntimeBridge(std::string policy_path,
                             std::string compositor_path,
                             const gw::protocol::x11::ScreenModel screen,
                             const std::chrono::milliseconds deadline)
    : policy_(std::move(policy_path), screen),
      compositor_(std::move(compositor_path), screen),
      deadline_duration_(deadline) {}

void RuntimeBridge::start(const Clock::time_point now) noexcept {
  policy_.disconnect();
  compositor_.disconnect();
  stage_ = Stage::Policy;
  deadline_ = now + deadline_duration_;
  retry_at_ = now;
  retry_index_ = 0;
}

void RuntimeBridge::schedule_retry(const Clock::time_point now) noexcept {
  const auto index =
      std::min<std::size_t>(retry_index_, kRetryDelays.size() - 1);
  retry_at_ = now + kRetryDelays[index];
  if (retry_index_ + 1 < kRetryDelays.size())
    ++retry_index_;
}

bool RuntimeBridge::service(const short policy_revents,
                            const short compositor_revents,
                            const Clock::time_point now, std::string &error) {
  if (stage_ == Stage::Failed)
    return false;
  if (stage_ == Stage::Ready) {
    std::string peer_error;
    if (!policy_.process(policy_revents, peer_error) ||
        !compositor_.process(compositor_revents, peer_error)) {
      policy_.disconnect();
      compositor_.disconnect();
      stage_ = Stage::Policy;
      deadline_ = now + deadline_duration_;
      retry_index_ = 0;
      schedule_retry(now);
    }
    return true;
  }
  if (stage_ != Stage::Ready && now >= deadline_) {
    stage_ = Stage::Failed;
    error = "integrated peer bootstrap deadline expired";
    return false;
  }
  if (stage_ == Stage::Policy) {
    if (policy_.state() == PeerBootstrapState::Disconnected &&
        now >= retry_at_) {
      std::string attempt_error;
      if (!policy_.connect(attempt_error))
        schedule_retry(now);
    }
    if (policy_.state() == PeerBootstrapState::Connecting ||
        policy_.state() == PeerBootstrapState::AwaitingReply) {
      if (!policy_.process(policy_revents, error)) {
        policy_.disconnect();
        schedule_retry(now);
      }
    }
    if (policy_.state() == PeerBootstrapState::Synchronized) {
      stage_ = Stage::Compositor;
      retry_at_ = now;
      retry_index_ = 0;
    }
  }
  if (stage_ == Stage::Compositor) {
    if (compositor_.state() == PeerBootstrapState::Disconnected &&
        now >= retry_at_) {
      std::string attempt_error;
      if (!compositor_.connect(attempt_error))
        schedule_retry(now);
    }
    if (compositor_.state() == PeerBootstrapState::Connecting ||
        compositor_.state() == PeerBootstrapState::AwaitingReply) {
      if (!compositor_.process(compositor_revents, error)) {
        compositor_.disconnect();
        schedule_retry(now);
      }
    }
    if (compositor_.state() == PeerBootstrapState::Synchronized)
      stage_ = Stage::Ready;
  }
  return true;
}

bool RuntimeBridge::ready() const noexcept { return stage_ == Stage::Ready; }

int RuntimeBridge::poll_timeout_ms(const Clock::time_point now) const noexcept {
  if (stage_ == Stage::Ready)
    return -1;
  const auto wake = std::min(deadline_, retry_at_);
  if (wake <= now)
    return 0;
  const auto count =
      std::chrono::duration_cast<std::chrono::milliseconds>(wake - now).count();
  return static_cast<int>(
      std::min<std::int64_t>(count, std::numeric_limits<int>::max()));
}

} // namespace glasswyrm::server
