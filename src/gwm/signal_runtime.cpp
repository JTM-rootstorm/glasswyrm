#include "gwm/signal_runtime.hpp"

#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

namespace glasswyrm::wm::runtime {
namespace {

int signal_write_fd = -1;

void wake_for_signal(int) {
  const std::uint8_t byte = 1;
  if (signal_write_fd >= 0) (void)::write(signal_write_fd, &byte, sizeof(byte));
}

}  // namespace

bool install_signal_runtime(SignalRuntime& runtime) {
  int descriptors[2] = {-1, -1};
  if (::pipe2(descriptors, O_NONBLOCK | O_CLOEXEC) != 0) {
    std::perror("gwm: signal pipe");
    return false;
  }
  runtime.read_fd = descriptors[0];
  runtime.write_fd = descriptors[1];
  signal_write_fd = runtime.write_fd;
  runtime.previous_int = std::signal(SIGINT, wake_for_signal);
  runtime.previous_term = std::signal(SIGTERM, wake_for_signal);
  return true;
}

void drain_signal_runtime(const SignalRuntime& runtime) {
  std::uint8_t bytes[32];
  while (::read(runtime.read_fd, bytes, sizeof(bytes)) > 0) {}
}

void close_signal_runtime(SignalRuntime& runtime,
                          const bool restore_handlers) {
  signal_write_fd = -1;
  if (restore_handlers) {
    (void)::signal(SIGINT, runtime.previous_int);
    (void)::signal(SIGTERM, runtime.previous_term);
  }
  (void)::close(runtime.read_fd);
  (void)::close(runtime.write_fd);
  runtime.read_fd = -1;
  runtime.write_fd = -1;
}

}  // namespace glasswyrm::wm::runtime
