#pragma once

#include <string>

namespace glasswyrm::compositor {

struct SignalEvents {
  bool stop{};
  bool virtual_terminal_release{};
  bool virtual_terminal_acquire{};
};

class SignalRuntime final {
 public:
  SignalRuntime() = default;
  ~SignalRuntime();
  SignalRuntime(const SignalRuntime&) = delete;
  SignalRuntime& operator=(const SignalRuntime&) = delete;

  [[nodiscard]] bool install(bool virtual_terminal_events,
                             std::string& error);
  [[nodiscard]] int poll_fd() const noexcept { return pipe_[0]; }
  [[nodiscard]] SignalEvents drain() noexcept;

 private:
  void restore() noexcept;

  int pipe_[2]{-1, -1};
  bool interrupt_installed_{};
  bool terminate_installed_{};
  bool release_installed_{};
  bool acquire_installed_{};
  struct SignalActionStorage;
  SignalActionStorage* actions_{};
};

}  // namespace glasswyrm::compositor
