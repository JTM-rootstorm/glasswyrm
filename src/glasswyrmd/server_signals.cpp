#include "glasswyrmd/server_runtime.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace glasswyrm::server {
namespace {

volatile std::sig_atomic_t runtime_stop_requested = 0;
volatile std::sig_atomic_t signal_write_descriptor = -1;

void request_stop_signal(int) {
  const int saved_errno = errno;
  runtime_stop_requested = 1;
  if (signal_write_descriptor >= 0) {
    const std::uint8_t byte = 1;
    const auto written = ::write(
        static_cast<int>(signal_write_descriptor), &byte, sizeof(byte));
    static_cast<void>(written);
  }
  errno = saved_errno;
}

}  // namespace

SignalRuntime::~SignalRuntime() { close(); }

bool SignalRuntime::start() {
  runtime_stop_requested = 0;
  if (::pipe2(descriptors_, O_CLOEXEC | O_NONBLOCK) != 0) {
    std::fprintf(stderr, "glasswyrmd: cannot create signal wakeup pipe: %s\n",
                 std::strerror(errno));
    return false;
  }
  signal_write_descriptor = descriptors_[1];
  struct sigaction action{};
  action.sa_handler = request_stop_signal;
  ::sigemptyset(&action.sa_mask);
  if (::sigaction(SIGINT, &action, nullptr) != 0 ||
      ::sigaction(SIGTERM, &action, nullptr) != 0) {
    std::fprintf(stderr, "glasswyrmd: cannot install signal handlers: %s\n",
                 std::strerror(errno));
    close();
    return false;
  }
  struct sigaction ignore_pipe{};
  ignore_pipe.sa_handler = SIG_IGN;
  ::sigemptyset(&ignore_pipe.sa_mask);
  (void)::sigaction(SIGPIPE, &ignore_pipe, nullptr);
  return true;
}

void SignalRuntime::close() noexcept {
  signal_write_descriptor = -1;
  if (descriptors_[0] >= 0) ::close(descriptors_[0]);
  if (descriptors_[1] >= 0) ::close(descriptors_[1]);
  descriptors_[0] = -1;
  descriptors_[1] = -1;
}

bool SignalRuntime::stop_requested() noexcept {
  return runtime_stop_requested != 0;
}

void SignalRuntime::request_stop() noexcept { runtime_stop_requested = 1; }

}  // namespace glasswyrm::server
