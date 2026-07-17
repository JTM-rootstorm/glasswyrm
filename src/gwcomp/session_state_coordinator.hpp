#pragma once

#include <glasswyrm/ipc.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace glasswyrm::compositor {

enum class CoordinatedSessionState {
  Disabled,
  Active,
  AwaitingInactive,
  Inactive,
  AwaitingActive,
  Failed,
};

struct SessionStateTiming {
  using Clock = std::chrono::steady_clock;
  std::chrono::milliseconds timeout{2000};
  std::function<Clock::time_point()> now{[] { return Clock::now(); }};
};

class SessionStateRequestSink {
 public:
  virtual ~SessionStateRequestSink() = default;
  [[nodiscard]] virtual bool enqueue(const gwipc_session_state_change &change,
                                     std::uint64_t &sequence,
                                     std::string &error) = 0;
};

class SessionStateCoordinator {
 public:
  explicit SessionStateCoordinator(SessionStateTiming timing = {})
      : timing_(std::move(timing)) {}

  void configure(bool negotiated) noexcept;
  [[nodiscard]] bool request_inactive(SessionStateRequestSink &sink,
                                      std::string &error);
  [[nodiscard]] bool request_active(SessionStateRequestSink &sink,
                                    std::string &error);
  [[nodiscard]] bool acknowledge(
      std::uint64_t reply_to,
      const gwipc_session_state_acknowledged &acknowledged,
      std::string &error);
  [[nodiscard]] bool check_timeout(std::string &error);
  void peer_disconnected() noexcept;

  [[nodiscard]] CoordinatedSessionState state() const noexcept {
    return state_;
  }
  [[nodiscard]] bool enabled() const noexcept {
    return state_ != CoordinatedSessionState::Disabled;
  }
  [[nodiscard]] bool waiting() const noexcept {
    return state_ == CoordinatedSessionState::AwaitingInactive ||
           state_ == CoordinatedSessionState::AwaitingActive;
  }
  [[nodiscard]] std::uint64_t generation() const noexcept {
    return generation_;
  }
  [[nodiscard]] int timeout_ms() const noexcept;

 private:
  [[nodiscard]] bool request(gwipc_session_state desired,
                             SessionStateRequestSink &sink,
                             std::string &error);
  void fail() noexcept { state_ = CoordinatedSessionState::Failed; }

  SessionStateTiming timing_;
  CoordinatedSessionState state_{CoordinatedSessionState::Disabled};
  std::uint64_t generation_{};
  std::uint64_t pending_sequence_{};
  gwipc_session_state pending_state_{GWIPC_SESSION_ACTIVE};
  SessionStateTiming::Clock::time_point deadline_{};
};

class GwipcSessionStateRequestSink final : public SessionStateRequestSink {
 public:
  explicit GwipcSessionStateRequestSink(gwipc_connection *connection)
      : connection_(connection) {}
  [[nodiscard]] bool enqueue(const gwipc_session_state_change &change,
                             std::uint64_t &sequence,
                             std::string &error) override;

 private:
  gwipc_connection *connection_{};
};

[[nodiscard]] bool decode_session_state_acknowledgement(
    const gwipc_message *message, gwipc_session_state_acknowledged &value,
    std::uint64_t &reply_to, std::string &error);

}  // namespace glasswyrm::compositor
