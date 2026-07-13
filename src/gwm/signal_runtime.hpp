#pragma once

#include <csignal>

namespace glasswyrm::wm::runtime {

using SignalHandler = void (*)(int);

struct SignalRuntime {
  int read_fd{-1};
  int write_fd{-1};
  SignalHandler previous_int{};
  SignalHandler previous_term{};
};

[[nodiscard]] bool install_signal_runtime(SignalRuntime& runtime);
void drain_signal_runtime(const SignalRuntime& runtime);
void close_signal_runtime(SignalRuntime& runtime, bool restore_handlers);

}  // namespace glasswyrm::wm::runtime
