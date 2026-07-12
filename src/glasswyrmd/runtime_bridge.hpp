#pragma once

#include "glasswyrmd/compositor_peer.hpp"
#include "glasswyrmd/policy_peer.hpp"

#include <chrono>
#include <string>

namespace glasswyrm::server {

class RuntimeBridge {
public:
  using Clock = std::chrono::steady_clock;

  RuntimeBridge(std::string policy_path, std::string compositor_path,
                gw::protocol::x11::ScreenModel screen,
                std::chrono::milliseconds deadline = std::chrono::seconds(10));

  void start(Clock::time_point now = Clock::now()) noexcept;
  [[nodiscard]] bool service(short policy_revents, short compositor_revents,
                             Clock::time_point now, std::string &error);
  [[nodiscard]] int policy_fd() const noexcept { return policy_.fd(); }
  [[nodiscard]] short policy_events() const noexcept {
    return policy_.wanted_events();
  }
  [[nodiscard]] int compositor_fd() const noexcept { return compositor_.fd(); }
  [[nodiscard]] short compositor_events() const noexcept {
    return compositor_.wanted_events();
  }
  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] int poll_timeout_ms(Clock::time_point now) const noexcept;

private:
  enum class Stage { Policy, Compositor, Ready, Failed };
  void schedule_retry(Clock::time_point now) noexcept;

  PolicyPeer policy_;
  CompositorPeer compositor_;
  Stage stage_{Stage::Policy};
  Clock::time_point deadline_{};
  Clock::time_point retry_at_{};
  std::chrono::milliseconds deadline_duration_;
  unsigned retry_index_{};
};

} // namespace glasswyrm::server
