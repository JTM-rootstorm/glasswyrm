#include "input/repeat_timer.hpp"

#include <sys/timerfd.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>

namespace glasswyrm::input {
namespace {

constexpr std::uint64_t kNanosecondsPerSecond = 1'000'000'000ULL;
constexpr std::uint64_t kNanosecondsPerMillisecond = 1'000'000ULL;
constexpr std::uint32_t kMaximumDispatchBound = 64;

std::string system_error(const char* action) {
  const int error_number = errno;
  std::string result(action);
  result += ": ";
  result += std::strerror(error_number);
  return result;
}

timespec nanoseconds_to_timespec(const std::uint64_t nanoseconds) noexcept {
  return timespec{
      static_cast<time_t>(nanoseconds / kNanosecondsPerSecond),
      static_cast<long>(nanoseconds % kNanosecondsPerSecond)};
}

}  // namespace

bool validate_repeat_config(const RepeatConfig& config, std::string& error) {
  error.clear();
  if (config.delay_ms == 0) {
    error = "repeat delay must be greater than zero milliseconds";
    return false;
  }
  if (config.rate_hz == 0 || config.rate_hz > kNanosecondsPerSecond) {
    error = "repeat rate must be between 1 and 1000000000 Hz";
    return false;
  }
  if (config.maximum_expirations_per_dispatch == 0 ||
      config.maximum_expirations_per_dispatch > kMaximumDispatchBound) {
    error = "repeat expiration dispatch bound must be between 1 and 64";
    return false;
  }
  return true;
}

std::unique_ptr<RepeatState> RepeatState::create(RepeatConfig config,
                                                 std::string& error) {
  if (!validate_repeat_config(config, error)) return nullptr;
  return std::unique_ptr<RepeatState>(new RepeatState(config));
}

RepeatTimerAction RepeatState::press(const std::uint8_t keycode,
                                     const std::uint32_t focus,
                                     const bool repeatable) noexcept {
  if (!repeatable) return RepeatTimerAction::None;
  active_key_ = RepeatKey{keycode, focus};
  return RepeatTimerAction::Arm;
}

RepeatTimerAction RepeatState::release(const std::uint8_t keycode) noexcept {
  if (!active_key_.has_value() || active_key_->keycode != keycode)
    return RepeatTimerAction::None;
  return cancel();
}

void RepeatState::focus_changed(const std::uint32_t focus,
                                const bool permit_retarget) noexcept {
  if (permit_retarget && active_key_.has_value()) active_key_->focus = focus;
}

RepeatTimerAction RepeatState::cancel_on_suspend() noexcept { return cancel(); }

RepeatTimerAction RepeatState::cancel_on_device_loss() noexcept {
  return cancel();
}

RepeatTimerAction RepeatState::cancel_on_client_cleanup() noexcept {
  return cancel();
}

RepeatBatch RepeatState::dispatch(const std::uint64_t expirations) const {
  RepeatBatch batch;
  if (!active_key_.has_value() || expirations == 0) return batch;

  batch.expirations_consumed = std::min<std::uint64_t>(
      expirations, config_.maximum_expirations_per_dispatch);
  batch.expirations_dropped = expirations - batch.expirations_consumed;
  batch.events.reserve(static_cast<std::size_t>(
      batch.expirations_consumed * 2U));
  for (std::uint64_t index = 0; index < batch.expirations_consumed; ++index) {
    // The M11 core subset freezes each repeat as release followed by press.
    batch.events.push_back({RepeatEventKind::KeyRelease, *active_key_});
    batch.events.push_back({RepeatEventKind::KeyPress, *active_key_});
  }
  return batch;
}

RepeatTimerAction RepeatState::cancel() noexcept {
  if (!active_key_.has_value()) return RepeatTimerAction::None;
  active_key_.reset();
  return RepeatTimerAction::Disarm;
}

std::unique_ptr<RepeatTimer> RepeatTimer::create(std::string& error) {
  error.clear();
  const int fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (fd < 0) {
    error = system_error("create repeat timerfd");
    return nullptr;
  }
  return std::unique_ptr<RepeatTimer>(new RepeatTimer(fd));
}

RepeatTimer::~RepeatTimer() {
  if (fd_ >= 0) (void)::close(fd_);
}

bool RepeatTimer::arm(const RepeatConfig& config, std::string& error) {
  if (!validate_repeat_config(config, error)) return false;

  const auto delay_nanoseconds =
      static_cast<std::uint64_t>(config.delay_ms) *
      kNanosecondsPerMillisecond;
  const auto interval_nanoseconds =
      kNanosecondsPerSecond / config.rate_hz;
  const itimerspec specification{
      nanoseconds_to_timespec(interval_nanoseconds),
      nanoseconds_to_timespec(delay_nanoseconds)};
  if (::timerfd_settime(fd_, 0, &specification, nullptr) != 0) {
    error = system_error("arm repeat timerfd");
    return false;
  }
  error.clear();
  return true;
}

bool RepeatTimer::disarm(std::string& error) {
  const itimerspec specification{};
  if (::timerfd_settime(fd_, 0, &specification, nullptr) != 0) {
    error = system_error("disarm repeat timerfd");
    return false;
  }
  error.clear();
  return true;
}

RepeatTimerRead RepeatTimer::read_expirations(std::string& error) {
  std::uint64_t expirations = 0;
  ssize_t result = 0;
  do {
    result = ::read(fd_, &expirations, sizeof(expirations));
  } while (result < 0 && errno == EINTR);

  if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    error.clear();
    return {RepeatTimerReadStatus::WouldBlock, 0};
  }
  if (result < 0) {
    error = system_error("read repeat timerfd");
    return {RepeatTimerReadStatus::Error, 0};
  }
  if (result != static_cast<ssize_t>(sizeof(expirations))) {
    error = "repeat timerfd returned a truncated expiration counter";
    return {RepeatTimerReadStatus::Error, 0};
  }
  error.clear();
  return {RepeatTimerReadStatus::Ready, expirations};
}

}  // namespace glasswyrm::input
