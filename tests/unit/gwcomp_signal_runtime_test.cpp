#include "gwcomp/signal_runtime.hpp"

#include "tests/helpers/test_support.hpp"

#include <csignal>
#include <poll.h>
#include <string>

namespace {

short ready(const int fd) {
  pollfd descriptor{fd, POLLIN, 0};
  gw::test::require(::poll(&descriptor, 1, 100) == 1,
                    "signal pipe becomes readable");
  return descriptor.revents;
}

}  // namespace

int main() {
  using glasswyrm::compositor::SignalRuntime;
  std::string error;
  {
    SignalRuntime runtime;
    gw::test::require(runtime.install(true, error) && error.empty(),
                      "tagged signal runtime installs");
    gw::test::require(::raise(SIGUSR1) == 0 && ::raise(SIGUSR2) == 0 &&
                          ::raise(SIGTERM) == 0,
                      "VT and stop signals are delivered");
    gw::test::require((ready(runtime.poll_fd()) & POLLIN) != 0,
                      "tagged signal pipe reports readiness");
    const auto events = runtime.drain();
    gw::test::require(events.stop && events.virtual_terminal_release &&
                          events.virtual_terminal_acquire,
                      "signal tags survive async-safe delivery");
    gw::test::require(runtime.drain().stop == false,
                      "signal pipe drains completely");

    SignalRuntime competing;
    gw::test::require(!competing.install(false, error),
                      "only one process signal runtime may be active");
  }

  const auto original_usr1 = std::signal(SIGUSR1, SIG_IGN);
  gw::test::require(original_usr1 != SIG_ERR,
                    "query original VT signal disposition");
  (void)std::signal(SIGUSR1, original_usr1);
  SignalRuntime headless;
  gw::test::require(headless.install(false, error),
                    "handlers restore for a later runtime");
  const auto observed_usr1 = std::signal(SIGUSR1, SIG_IGN);
  gw::test::require(observed_usr1 == original_usr1,
                    "headless runtime leaves VT signal disposition untouched");
  (void)std::signal(SIGUSR1, observed_usr1);
  gw::test::require(::raise(SIGINT) == 0,
                    "headless stop signal is delivered");
  gw::test::require((ready(headless.poll_fd()) & POLLIN) != 0 &&
                        headless.drain().stop,
                    "headless signal event drains");
  return 0;
}
