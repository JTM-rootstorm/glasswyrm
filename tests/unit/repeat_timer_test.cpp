#include "input/repeat_timer.hpp"

#include "helpers/test_support.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/timerfd.h>

#include <cstdint>
#include <string>
#include <vector>

using glasswyrm::input::RepeatConfig;
using glasswyrm::input::RepeatEvent;
using glasswyrm::input::RepeatEventKind;
using glasswyrm::input::RepeatKey;
using glasswyrm::input::RepeatState;
using glasswyrm::input::RepeatTimer;
using glasswyrm::input::RepeatTimerAction;
using glasswyrm::input::RepeatTimerReadStatus;
using gw::test::require;

int main() {
  std::string error;
  auto state = RepeatState::create(RepeatConfig{}, error);
  require(state != nullptr && error.empty() && state->config().delay_ms == 500 &&
              state->config().rate_hz == 25,
          "default repeat delay and rate");
  require(state->press(50, 10, false) == RepeatTimerAction::None &&
              !state->active_key().has_value(),
          "nonrepeatable keys never start repeat");
  require(state->press(38, 10, true) == RepeatTimerAction::Arm &&
              state->active_key() == RepeatKey{38, 10},
          "repeatable press arms the most recent key");

  auto batch = state->dispatch(2);
  require(batch.expirations_consumed == 2 && batch.expirations_dropped == 0 &&
              batch.events ==
                  std::vector<RepeatEvent>{
                      {RepeatEventKind::KeyRelease, {38, 10}},
                      {RepeatEventKind::KeyPress, {38, 10}},
                      {RepeatEventKind::KeyRelease, {38, 10}},
                      {RepeatEventKind::KeyPress, {38, 10}}},
          "repeat packet order is frozen as release then press");
  require(state->press(56, 20, true) == RepeatTimerAction::Arm &&
              state->active_key() == RepeatKey{56, 20} &&
              state->release(38) == RepeatTimerAction::None,
          "new repeatable press switches key and old release is inert");
  state->focus_changed(30, false);
  require(state->active_key() == RepeatKey{56, 20},
          "focus remains stable when retarget policy rejects the change");
  state->focus_changed(30, true);
  require(state->active_key() == RepeatKey{56, 30} &&
              state->dispatch(1).events.front().key.focus == 30,
          "permitted focus changes retarget later repeats");
  require(state->release(56) == RepeatTimerAction::Disarm &&
              !state->active_key().has_value() && state->dispatch(1).events.empty(),
          "active-key release cancels repeat");

  require(state->press(38, 10, true) == RepeatTimerAction::Arm &&
              state->cancel_on_suspend() == RepeatTimerAction::Disarm &&
              state->cancel_on_suspend() == RepeatTimerAction::None,
          "suspend cancellation is idempotent");
  require(state->press(38, 10, true) == RepeatTimerAction::Arm &&
              state->cancel_on_device_loss() == RepeatTimerAction::Disarm,
          "device loss cancels repeat");
  require(state->press(38, 10, true) == RepeatTimerAction::Arm &&
              state->cancel_on_client_cleanup() == RepeatTimerAction::Disarm,
          "client cleanup cancels repeat");

  RepeatConfig bounded;
  bounded.maximum_expirations_per_dispatch = 3;
  auto bounded_state = RepeatState::create(bounded, error);
  require(bounded_state != nullptr &&
              bounded_state->press(38, 1, true) == RepeatTimerAction::Arm,
          "bounded repeat state creation");
  batch = bounded_state->dispatch(100);
  require(batch.expirations_consumed == 3 && batch.expirations_dropped == 97 &&
              batch.events.size() == 6,
          "timer overrun work is explicitly bounded and accounted");

  RepeatConfig invalid;
  invalid.delay_ms = 0;
  require(RepeatState::create(invalid, error) == nullptr && !error.empty(),
          "zero repeat delay is rejected");
  invalid = RepeatConfig{};
  invalid.rate_hz = 0;
  require(RepeatState::create(invalid, error) == nullptr && !error.empty(),
          "zero repeat rate is rejected");
  invalid = RepeatConfig{};
  invalid.maximum_expirations_per_dispatch = 65;
  require(RepeatState::create(invalid, error) == nullptr && !error.empty(),
          "unbounded expiration work is rejected");

  auto timer = RepeatTimer::create(error);
  require(timer != nullptr && error.empty(), "timerfd creation");
  const auto descriptor_flags = ::fcntl(timer->fd(), F_GETFD);
  const auto status_flags = ::fcntl(timer->fd(), F_GETFL);
  require(descriptor_flags >= 0 && (descriptor_flags & FD_CLOEXEC) != 0 &&
              status_flags >= 0 && (status_flags & O_NONBLOCK) != 0,
          "repeat timerfd is close-on-exec and nonblocking");
  auto timer_read = timer->read_expirations(error);
  require(timer_read.status == RepeatTimerReadStatus::WouldBlock && error.empty(),
          "unarmed nonblocking timer reports would-block");

  require(timer->arm(RepeatConfig{}, error), "default timer arm");
  itimerspec specification{};
  require(::timerfd_gettime(timer->fd(), &specification) == 0 &&
              specification.it_value.tv_sec == 0 &&
              specification.it_value.tv_nsec > 0 &&
              specification.it_value.tv_nsec <= 500'000'000 &&
              specification.it_interval.tv_sec == 0 &&
              specification.it_interval.tv_nsec == 40'000'000,
          "timerfd uses configured initial delay and repeat rate");

  RepeatConfig fast;
  fast.delay_ms = 5;
  fast.rate_hz = 100;
  require(timer->arm(fast, error), "fast timer arm");
  pollfd ready{timer->fd(), POLLIN, 0};
  require(::poll(&ready, 1, 1000) == 1 && (ready.revents & POLLIN) != 0,
          "armed timer becomes poll-readable");
  timer_read = timer->read_expirations(error);
  require(timer_read.status == RepeatTimerReadStatus::Ready &&
              timer_read.expirations >= 1 && error.empty(),
          "ready timer returns accumulated expirations");
  require(timer->disarm(error) &&
              timer->read_expirations(error).status ==
                  RepeatTimerReadStatus::WouldBlock,
          "disarm clears timer readiness");
}
