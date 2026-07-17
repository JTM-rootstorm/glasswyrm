#pragma once

#include "backends/session/vt_api.hpp"

#include <string>
#include <string_view>

namespace glasswyrm::session {

enum class DirectSessionState { Empty, Active, Suspended, Failed, Restored };

struct VirtualTerminalSignals {
  int release_signal{};
  int acquire_signal{};
};

class DisplaySessionControl {
public:
  virtual ~DisplaySessionControl() = default;

  [[nodiscard]] virtual bool quiesce_pending_flip(std::string &error) = 0;
  [[nodiscard]] virtual bool acquire_master(std::string &error) = 0;
  [[nodiscard]] virtual bool drop_master(std::string &error) = 0;
  [[nodiscard]] virtual bool present_committed_frame(std::string &error) = 0;
  [[nodiscard]] virtual bool restore_original_display(std::string &error) = 0;
  [[nodiscard]] virtual bool release_scanout_resources(std::string &error) = 0;
};

class DirectVirtualTerminalSession final {
public:
  DirectVirtualTerminalSession(VirtualTerminalApi &api,
                               DisplaySessionControl &display) noexcept;
  ~DirectVirtualTerminalSession();

  DirectVirtualTerminalSession(const DirectVirtualTerminalSession &) = delete;
  DirectVirtualTerminalSession &
  operator=(const DirectVirtualTerminalSession &) = delete;

  [[nodiscard]] bool acquire(std::string_view path,
                             VirtualTerminalSignals signals,
                             std::string &error);
  [[nodiscard]] bool release(std::string &error);
  [[nodiscard]] bool reacquire(std::string &error);
  [[nodiscard]] bool restore(std::string &error);

  [[nodiscard]] DirectSessionState state() const noexcept { return state_; }
  [[nodiscard]] int terminal_fd() const noexcept { return terminal_fd_; }
  [[nodiscard]] unsigned terminal_number() const noexcept {
    return terminal_number_;
  }
  [[nodiscard]] unsigned previous_active_terminal() const noexcept {
    return saved_state_.active;
  }

private:
  void append_api_error(std::string_view operation, std::string &error) const;
  static void append_error(std::string_view detail, std::string &error);
  void unwind_failed_acquire(std::string &error);

  VirtualTerminalApi &api_;
  DisplaySessionControl &display_;
  DirectSessionState state_{DirectSessionState::Empty};
  int terminal_fd_{-1};
  unsigned terminal_number_{};
  VirtualTerminalState saved_state_{};
  VirtualTerminalMode saved_mode_{};
  int saved_kd_mode_{};
  int saved_keyboard_mode_{};
  bool have_state_{};
  bool have_mode_{};
  bool have_kd_mode_{};
  bool have_keyboard_mode_{};
  bool activated_{};
  bool keyboard_mode_set_{};
  bool process_mode_set_{};
  bool graphics_mode_set_{};
  bool master_owned_{};
};

} // namespace glasswyrm::session
