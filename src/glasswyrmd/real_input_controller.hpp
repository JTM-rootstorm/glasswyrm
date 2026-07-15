#pragma once

#include "input/libinput_backend.hpp"
#include "input/repeat_timer.hpp"
#include "input/xkb_keymap.hpp"

#include <glasswyrm/ipc/session.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace glasswyrm::server {

enum class RealInputEventKind { Motion, Button, Key, StateReset };

struct RealInputEvent {
  RealInputEventKind kind{RealInputEventKind::Motion};
  std::uint32_t time_ms{};
  std::int32_t root_x{};
  std::int32_t root_y{};
  std::uint32_t focus_window{};
  std::uint8_t detail{};
  bool pressed{};
  std::uint16_t state_before{};
  std::uint16_t state_after{};
};

struct RealInputControllerConfig {
  std::vector<std::string> device_paths;
  glasswyrm::input::XkbKeymapConfig keymap;
  glasswyrm::input::RepeatConfig repeat;
  std::uint32_t root_width{};
  std::uint32_t root_height{};
};

struct RealInputServiceResult {
  bool success{true};
  bool input_unavailable{};
  std::string error;
};

struct RealInputSessionResult {
  gwipc_session_state_result result{GWIPC_SESSION_STATE_FAILED};
  bool fatal{};
  bool reset_server_state{};
  std::string error;
};

class RealInputController {
public:
  [[nodiscard]] static std::unique_ptr<RealInputController>
  create(std::unique_ptr<glasswyrm::input::LibinputApi> api,
         RealInputControllerConfig config, std::string &error);

  [[nodiscard]] int input_fd() const noexcept { return backend_.poll_fd(); }
  [[nodiscard]] int repeat_fd() const noexcept { return repeat_timer_->fd(); }
  [[nodiscard]] bool active() const noexcept { return backend_.active(); }
  [[nodiscard]] bool ready() const noexcept {
    return backend_.active() && backend_.readiness().ready();
  }
  [[nodiscard]] bool has_events() const noexcept { return !events_.empty(); }
  [[nodiscard]] bool backend_work_pending() const noexcept {
    return backend_work_pending_;
  }
  [[nodiscard]] std::size_t queued_event_count() const noexcept {
    return events_.size();
  }

  [[nodiscard]] RealInputServiceResult
  service_backend(std::uint32_t focus_window);
  [[nodiscard]] RealInputServiceResult service_repeat();
  [[nodiscard]] RealInputSessionResult
  apply_session_state(gwipc_session_state state);
  [[nodiscard]] std::optional<RealInputEvent> take_event();
  void focus_changed(std::uint32_t focus_window) noexcept;
  void client_cleanup() noexcept;

private:
  RealInputController(std::unique_ptr<glasswyrm::input::LibinputApi> api,
                      std::unique_ptr<glasswyrm::input::XkbKeymap> keymap,
                      std::unique_ptr<glasswyrm::input::RepeatState> repeat,
                      std::unique_ptr<glasswyrm::input::RepeatTimer> timer);

  [[nodiscard]] RealInputServiceResult
  convert(std::span<const glasswyrm::input::RealInputRecord> records,
          std::uint32_t focus_window);
  [[nodiscard]] bool
  apply_repeat_action(glasswyrm::input::RepeatTimerAction action,
                      std::string &error);
  [[nodiscard]] std::uint16_t state_mask() const noexcept;
  [[nodiscard]] std::uint32_t next_repeat_time() noexcept;
  void reset_provider_state(bool publish_reset, bool clear_events,
                            std::string &error);

  static constexpr std::size_t kMaximumQueuedEvents = 4096;
  std::unique_ptr<glasswyrm::input::LibinputApi> api_;
  glasswyrm::input::LibinputBackend backend_;
  std::unique_ptr<glasswyrm::input::XkbKeymap> keymap_;
  std::unique_ptr<glasswyrm::input::RepeatState> repeat_;
  std::unique_ptr<glasswyrm::input::RepeatTimer> repeat_timer_;
  std::deque<RealInputEvent> events_;
  std::uint16_t button_mask_{};
  std::uint32_t last_time_ms_{1};
  bool backend_work_pending_{};
  bool availability_reported_{true};
};

} // namespace glasswyrm::server
