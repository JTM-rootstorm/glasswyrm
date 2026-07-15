#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace glasswyrm::input {

struct RepeatConfig {
  std::uint32_t delay_ms{500};
  std::uint32_t rate_hz{25};
  std::uint32_t maximum_expirations_per_dispatch{8};
};

[[nodiscard]] bool validate_repeat_config(const RepeatConfig& config,
                                          std::string& error);

enum class RepeatTimerAction { None, Arm, Disarm };
enum class RepeatEventKind { KeyRelease, KeyPress };

struct RepeatKey {
  std::uint8_t keycode{0};
  std::uint32_t focus{0};

  [[nodiscard]] bool operator==(const RepeatKey&) const noexcept = default;
};

struct RepeatEvent {
  RepeatEventKind kind{RepeatEventKind::KeyRelease};
  RepeatKey key;

  [[nodiscard]] bool operator==(const RepeatEvent&) const noexcept = default;
};

struct RepeatBatch {
  std::vector<RepeatEvent> events;
  std::uint64_t expirations_consumed{0};
  std::uint64_t expirations_dropped{0};
};

class RepeatState {
 public:
  [[nodiscard]] static std::unique_ptr<RepeatState> create(
      RepeatConfig config, std::string& error);

  [[nodiscard]] const RepeatConfig& config() const noexcept { return config_; }
  [[nodiscard]] const std::optional<RepeatKey>& active_key() const noexcept {
    return active_key_;
  }

  [[nodiscard]] RepeatTimerAction press(std::uint8_t keycode,
                                        std::uint32_t focus,
                                        bool repeatable) noexcept;
  [[nodiscard]] RepeatTimerAction release(std::uint8_t keycode) noexcept;
  void focus_changed(std::uint32_t focus, bool permit_retarget) noexcept;
  [[nodiscard]] RepeatTimerAction cancel_on_suspend() noexcept;
  [[nodiscard]] RepeatTimerAction cancel_on_device_loss() noexcept;
  [[nodiscard]] RepeatTimerAction cancel_on_client_cleanup() noexcept;
  [[nodiscard]] RepeatBatch dispatch(std::uint64_t expirations) const;

 private:
  explicit RepeatState(RepeatConfig config) noexcept : config_(config) {}
  [[nodiscard]] RepeatTimerAction cancel() noexcept;

  RepeatConfig config_;
  std::optional<RepeatKey> active_key_;
};

enum class RepeatTimerReadStatus { Ready, WouldBlock, Error };

struct RepeatTimerRead {
  RepeatTimerReadStatus status{RepeatTimerReadStatus::WouldBlock};
  std::uint64_t expirations{0};
};

class RepeatTimer {
 public:
  [[nodiscard]] static std::unique_ptr<RepeatTimer> create(std::string& error);

  ~RepeatTimer();
  RepeatTimer(const RepeatTimer&) = delete;
  RepeatTimer& operator=(const RepeatTimer&) = delete;

  [[nodiscard]] int fd() const noexcept { return fd_; }
  [[nodiscard]] bool arm(const RepeatConfig& config, std::string& error);
  [[nodiscard]] bool disarm(std::string& error);
  [[nodiscard]] RepeatTimerRead read_expirations(std::string& error);

 private:
  explicit RepeatTimer(int fd) noexcept : fd_(fd) {}

  int fd_{-1};
};

}  // namespace glasswyrm::input
