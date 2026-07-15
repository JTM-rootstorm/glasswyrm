#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace glasswyrm::input {
struct CursorImage;
}

namespace glasswyrm::server {

using GrabClientId = std::uint64_t;

inline constexpr std::uint16_t kAnyModifier = 0x8000U;
inline constexpr std::uint8_t kAnyButton = 0;

enum class GrabMode : std::uint8_t { Synchronous = 0, Asynchronous = 1 };

enum class GrabStatus {
  Success,
  AlreadyGrabbed,
  InvalidTime,
  NotViewable,
  InvalidMode,
  InvalidEventMask,
  UnsupportedConfine,
  InvalidCursor,
  InvalidButton,
  InvalidModifiers,
  BadAccess,
  NotFound,
  NotOwner,
  BadImplementation,
};

enum class PointerGrabOrigin { Explicit, AutomaticButton, PassiveButton };

struct PointerGrabRequest {
  GrabClientId client{0};
  std::uint32_t window{0};
  bool owner_events{false};
  std::uint32_t event_mask{0};
  GrabMode pointer_mode{GrabMode::Asynchronous};
  GrabMode keyboard_mode{GrabMode::Asynchronous};
  std::uint32_t confine_to{0};
  std::uint32_t cursor{0};
  bool cursor_valid{true};
  std::shared_ptr<const input::CursorImage> cursor_image;
  std::uint32_t request_time{0};
  std::uint32_t current_time{0};
  bool window_viewable{true};
};

struct KeyboardGrabRequest {
  GrabClientId client{0};
  std::uint32_t window{0};
  bool owner_events{false};
  GrabMode pointer_mode{GrabMode::Asynchronous};
  GrabMode keyboard_mode{GrabMode::Asynchronous};
  std::uint32_t request_time{0};
  std::uint32_t current_time{0};
  bool window_viewable{true};
};

struct PassiveButtonGrabRequest {
  GrabClientId client{0};
  std::uint32_t window{0};
  std::uint8_t button{kAnyButton};
  std::uint16_t modifiers{kAnyModifier};
  bool owner_events{false};
  std::uint32_t event_mask{0};
  GrabMode pointer_mode{GrabMode::Asynchronous};
  GrabMode keyboard_mode{GrabMode::Asynchronous};
  std::uint32_t confine_to{0};
  std::uint32_t cursor{0};
  bool cursor_valid{true};
  std::shared_ptr<const input::CursorImage> cursor_image;
};

struct PointerGrab {
  PointerGrabOrigin origin{PointerGrabOrigin::Explicit};
  GrabClientId client{0};
  std::uint32_t window{0};
  bool owner_events{false};
  std::uint32_t event_mask{0};
  std::uint32_t cursor{0};
  std::shared_ptr<const input::CursorImage> cursor_image;
  std::uint32_t activation_time{0};
  std::uint16_t held_buttons{0};
};

struct KeyboardGrab {
  GrabClientId client{0};
  std::uint32_t window{0};
  bool owner_events{false};
  std::uint32_t activation_time{0};
};

enum class GrabRouteKind { Natural, GrabWindow, Suppressed };

struct GrabRouteInput {
  GrabClientId natural_client{0};
  std::uint32_t natural_window{0};
  std::uint32_t event_mask{0};
  std::uint32_t real_pointer_target{0};
};

struct GrabRouteDecision {
  GrabRouteKind kind{GrabRouteKind::Natural};
  GrabClientId client{0};
  std::uint32_t window{0};
  std::uint32_t crossing_target{0};
};

enum class AllowEventsMode : std::uint8_t {
  AsyncPointer = 0,
  SyncPointer = 1,
  ReplayPointer = 2,
  AsyncKeyboard = 3,
  SyncKeyboard = 4,
  ReplayKeyboard = 5,
  AsyncBoth = 6,
  SyncBoth = 7,
};

struct GrabCleanupResult {
  bool pointer_released{false};
  bool keyboard_released{false};
  std::size_t passive_buttons_removed{0};
};

class GrabState {
 public:
  [[nodiscard]] const std::optional<PointerGrab>& pointer_grab() const noexcept {
    return pointer_grab_;
  }
  [[nodiscard]] const std::optional<KeyboardGrab>& keyboard_grab() const noexcept {
    return keyboard_grab_;
  }
  [[nodiscard]] std::size_t passive_button_count() const noexcept {
    return passive_buttons_.size();
  }

  [[nodiscard]] GrabStatus grab_pointer(const PointerGrabRequest& request) noexcept;
  [[nodiscard]] bool ungrab_pointer(GrabClientId client,
                                    std::uint32_t request_time,
                                    std::uint32_t current_time) noexcept;
  [[nodiscard]] GrabStatus change_active_pointer_grab(
      GrabClientId client, std::uint32_t event_mask, std::uint32_t cursor,
      bool cursor_valid, std::shared_ptr<const input::CursorImage> cursor_image,
      std::uint32_t request_time,
      std::uint32_t current_time) noexcept;

  [[nodiscard]] GrabStatus grab_keyboard(
      const KeyboardGrabRequest& request) noexcept;
  [[nodiscard]] bool ungrab_keyboard(GrabClientId client,
                                     std::uint32_t request_time,
                                     std::uint32_t current_time) noexcept;

  [[nodiscard]] bool begin_automatic_button_grab(
      GrabClientId client, std::uint32_t window, std::uint8_t button,
      std::uint32_t current_time) noexcept;
  void note_button_press(std::uint8_t button) noexcept;
  [[nodiscard]] bool note_button_release(std::uint8_t button) noexcept;

  [[nodiscard]] GrabStatus grab_button(
      const PassiveButtonGrabRequest& request);
  [[nodiscard]] std::size_t ungrab_button(GrabClientId client,
                                          std::uint32_t window,
                                          std::uint8_t button,
                                          std::uint16_t modifiers) noexcept;
  [[nodiscard]] bool activate_passive_button(
      std::uint8_t button, std::uint16_t modifiers,
      std::uint32_t current_time) noexcept;

  [[nodiscard]] GrabRouteDecision route_pointer(
      const GrabRouteInput& input) const noexcept;
  [[nodiscard]] GrabRouteDecision route_keyboard(
      const GrabRouteInput& input) const noexcept;
  [[nodiscard]] GrabStatus allow_events(AllowEventsMode mode,
                                        std::uint32_t request_time,
                                        std::uint32_t current_time) const noexcept;

  [[nodiscard]] GrabCleanupResult cleanup_client(GrabClientId client) noexcept;
  [[nodiscard]] GrabCleanupResult cleanup_window(std::uint32_t window) noexcept;
  [[nodiscard]] GrabCleanupResult suspend() noexcept;

 private:
  struct PassiveButtonGrab {
    PassiveButtonGrabRequest request;
    std::uint64_t serial{0};
  };

  [[nodiscard]] static bool valid_time(std::uint32_t request_time,
                                       std::uint32_t current_time,
                                       std::uint32_t last_grab_time,
                                       std::uint32_t& resolved) noexcept;
  [[nodiscard]] static GrabStatus validate_pointer_fields(
      std::uint32_t event_mask, GrabMode pointer_mode,
      GrabMode keyboard_mode, std::uint32_t confine_to,
      std::uint32_t cursor, bool cursor_valid) noexcept;
  [[nodiscard]] static bool passive_overlaps(
      const PassiveButtonGrabRequest& left,
      const PassiveButtonGrabRequest& right) noexcept;
  [[nodiscard]] GrabCleanupResult release_active() noexcept;

  std::optional<PointerGrab> pointer_grab_;
  std::optional<KeyboardGrab> keyboard_grab_;
  std::vector<PassiveButtonGrab> passive_buttons_;
  std::uint32_t last_pointer_grab_time_{0};
  std::uint32_t last_keyboard_grab_time_{0};
  std::uint64_t next_passive_serial_{1};
};

}  // namespace glasswyrm::server
