#pragma once

#include "wm/types.hpp"

#include <cstdint>
#include <optional>

namespace glasswyrm::wm {

inline constexpr std::uint16_t kInteractiveMod1 = 1U << 3U;
inline constexpr std::uint32_t kInteractiveF4Keysym = 0xffc1U;

struct InteractiveBindings {
  std::uint16_t move_modifiers{kInteractiveMod1};
  std::uint16_t resize_modifiers{kInteractiveMod1};
  std::uint16_t close_modifiers{kInteractiveMod1};
  std::uint8_t move_button{1};
  std::uint8_t resize_button{3};
  std::uint32_t close_keysym{kInteractiveF4Keysym};
  std::uint32_t minimum_width{96};
  std::uint32_t minimum_height{64};
  bool raise_on_focus{true};
  bool consume_wm_bindings{true};
};

enum class InteractionKind { None, Move, ResizeBottomRight };
enum class InteractionCursor { None, FleurMove, BottomRightResize };

struct PointerPosition {
  std::int32_t x{0};
  std::int32_t y{0};

  [[nodiscard]] bool
  operator==(const PointerPosition &) const noexcept = default;
};

struct InteractiveGeometry {
  std::int32_t x{0};
  std::int32_t y{0};
  std::uint32_t width{0};
  std::uint32_t height{0};

  [[nodiscard]] bool
  operator==(const InteractiveGeometry &) const noexcept = default;
};

struct InteractionBegin {
  InteractionKind kind{InteractionKind::None};
  std::uint32_t target{0};
  std::uint8_t button{0};
  PointerPosition pointer;
  InteractiveGeometry applied_geometry;
  bool managed{false};
  bool visible{false};
  bool direct_root{false};
  bool input_output{false};
};

struct InteractionBeginResult {
  bool accepted{false};
  bool consume_event{false};
  bool focus{false};
  bool raise{false};
  InteractionCursor cursor{InteractionCursor::None};
};

class InteractivePolicy {
public:
  explicit InteractivePolicy(InteractiveBindings bindings = {}) noexcept
      : bindings_(bindings) {}

  [[nodiscard]] InteractionBeginResult
  begin(const InteractionBegin &request) noexcept;
  void motion(PointerPosition pointer) noexcept;
  [[nodiscard]] std::optional<InteractiveGeometry>
  take_geometry_request() noexcept;
  [[nodiscard]] bool complete_geometry(InteractiveGeometry applied) noexcept;
  [[nodiscard]] bool release(std::uint8_t button,
                             PointerPosition pointer) noexcept;
  [[nodiscard]] bool confirm_cursor_published() noexcept;
  [[nodiscard]] bool finish_ready() const noexcept;
  [[nodiscard]] bool finish() noexcept;
  [[nodiscard]] bool abort() noexcept;

  [[nodiscard]] InteractionKind kind() const noexcept { return kind_; }
  [[nodiscard]] std::uint32_t target() const noexcept { return target_; }
  [[nodiscard]] bool transaction_in_flight() const noexcept {
    return transaction_in_flight_;
  }
  [[nodiscard]] InteractionCursor cursor() const noexcept { return cursor_; }
  [[nodiscard]] bool cursor_published() const noexcept {
    return cursor_published_;
  }
  [[nodiscard]] const InteractiveGeometry &last_committed() const noexcept {
    return last_committed_;
  }

private:
  InteractiveBindings bindings_;
  InteractionKind kind_{InteractionKind::None};
  InteractionCursor cursor_{InteractionCursor::None};
  std::uint32_t target_{0};
  std::uint8_t button_{0};
  PointerPosition initial_pointer_;
  PointerPosition latest_pointer_;
  std::optional<PointerPosition> dispatched_pointer_;
  InteractiveGeometry initial_geometry_;
  InteractiveGeometry last_committed_;
  bool transaction_in_flight_{false};
  bool ending_{false};
  bool cursor_published_{false};
};

enum class CloseAction { None, SendDeleteWindow, DestroyTopLevel };

struct CloseDecision {
  CloseAction action{CloseAction::None};
  bool consume_event{false};
  std::uint32_t target{0};
  std::uint32_t event_time{0};
};

[[nodiscard]] CloseDecision evaluate_close_binding(
    const InteractiveBindings &bindings, std::uint16_t modifiers,
    std::uint32_t keysym, std::uint32_t focused_window,
    bool focused_managed_top_level, bool focused_override_redirect,
    bool supports_delete_window, std::uint32_t event_time) noexcept;

} // namespace glasswyrm::wm
