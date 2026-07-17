#include "wm/interactive_policy.hpp"

#include <algorithm>
#include <limits>

namespace glasswyrm::wm {
namespace {

std::int32_t add_coordinate(const std::int32_t origin,
                            const std::int64_t delta) noexcept {
  return static_cast<std::int32_t>(
      std::clamp<std::int64_t>(static_cast<std::int64_t>(origin) + delta,
                               std::numeric_limits<std::int32_t>::min(),
                               std::numeric_limits<std::int32_t>::max()));
}

std::uint32_t resize_extent(const std::uint32_t origin,
                            const std::int64_t delta,
                            const std::uint32_t minimum) noexcept {
  return static_cast<std::uint32_t>(
      std::clamp<std::int64_t>(static_cast<std::int64_t>(origin) + delta,
                               minimum, maximum_window_extent));
}

} // namespace

InteractionBeginResult
InteractivePolicy::begin(const InteractionBegin &request) noexcept {
  InteractionBeginResult result;
  if (kind_ != InteractionKind::None ||
      (request.kind != InteractionKind::Move &&
       request.kind != InteractionKind::ResizeBottomRight) ||
      request.target == 0 || request.button == 0 ||
      request.applied_geometry.width == 0 ||
      request.applied_geometry.height == 0 || !request.managed ||
      !request.visible || !request.direct_root || !request.input_output)
    return result;

  kind_ = request.kind;
  target_ = request.target;
  button_ = request.button;
  initial_pointer_ = request.pointer;
  latest_pointer_ = request.pointer;
  dispatched_pointer_.reset();
  initial_geometry_ = request.applied_geometry;
  last_committed_ = request.applied_geometry;
  transaction_in_flight_ = false;
  ending_ = false;
  cursor_published_ = false;
  cursor_ = kind_ == InteractionKind::Move
                ? InteractionCursor::FleurMove
                : InteractionCursor::BottomRightResize;
  result.accepted = true;
  result.consume_event = bindings_.consume_wm_bindings;
  result.focus = true;
  result.raise = bindings_.raise_on_focus;
  result.cursor = cursor_;
  return result;
}

void InteractivePolicy::motion(const PointerPosition pointer) noexcept {
  if (kind_ != InteractionKind::None)
    latest_pointer_ = pointer;
}

std::optional<InteractiveGeometry>
InteractivePolicy::take_geometry_request() noexcept {
  if (kind_ == InteractionKind::None || transaction_in_flight_ ||
      latest_pointer_ == initial_pointer_ ||
      (dispatched_pointer_ && *dispatched_pointer_ == latest_pointer_))
    return std::nullopt;
  const auto delta_x =
      static_cast<std::int64_t>(latest_pointer_.x) - initial_pointer_.x;
  const auto delta_y =
      static_cast<std::int64_t>(latest_pointer_.y) - initial_pointer_.y;
  auto geometry = initial_geometry_;
  if (kind_ == InteractionKind::Move) {
    geometry.x = add_coordinate(initial_geometry_.x, delta_x);
    geometry.y = add_coordinate(initial_geometry_.y, delta_y);
  } else {
    geometry.width = resize_extent(initial_geometry_.width, delta_x,
                                   bindings_.minimum_width);
    geometry.height = resize_extent(initial_geometry_.height, delta_y,
                                    bindings_.minimum_height);
  }
  dispatched_pointer_ = latest_pointer_;
  transaction_in_flight_ = true;
  return geometry;
}

bool InteractivePolicy::complete_geometry(
    const InteractiveGeometry applied) noexcept {
  if (!transaction_in_flight_ || applied.width == 0 || applied.height == 0)
    return false;
  last_committed_ = applied;
  transaction_in_flight_ = false;
  return true;
}

bool InteractivePolicy::release(const std::uint8_t button,
                                const PointerPosition pointer) noexcept {
  if (kind_ == InteractionKind::None || button != button_)
    return false;
  latest_pointer_ = pointer;
  ending_ = true;
  return true;
}

bool InteractivePolicy::confirm_cursor_published() noexcept {
  if (kind_ == InteractionKind::None)
    return false;
  cursor_published_ = true;
  return true;
}

bool InteractivePolicy::finish_ready() const noexcept {
  if (!ending_ || transaction_in_flight_ || !cursor_published_)
    return false;
  return latest_pointer_ == initial_pointer_ ||
         (dispatched_pointer_ && *dispatched_pointer_ == latest_pointer_);
}

bool InteractivePolicy::finish() noexcept {
  if (!finish_ready())
    return false;
  return abort();
}

bool InteractivePolicy::abort() noexcept {
  if (kind_ == InteractionKind::None)
    return false;
  kind_ = InteractionKind::None;
  cursor_ = InteractionCursor::None;
  target_ = 0;
  button_ = 0;
  dispatched_pointer_.reset();
  transaction_in_flight_ = false;
  ending_ = false;
  cursor_published_ = false;
  return true;
}

CloseDecision evaluate_close_binding(const InteractiveBindings &bindings,
                                     const std::uint16_t modifiers,
                                     const std::uint32_t keysym,
                                     const std::uint32_t focused_window,
                                     const bool focused_managed_top_level,
                                     const bool focused_override_redirect,
                                     const bool supports_delete_window,
                                     const std::uint32_t event_time) noexcept {
  CloseDecision result;
  if (modifiers != bindings.close_modifiers ||
      keysym != bindings.close_keysym || focused_window == 0 ||
      !focused_managed_top_level || focused_override_redirect)
    return result;
  result.action = supports_delete_window ? CloseAction::SendDeleteWindow
                                         : CloseAction::DestroyTopLevel;
  result.consume_event = bindings.consume_wm_bindings;
  result.target = focused_window;
  result.event_time = event_time;
  return result;
}

} // namespace glasswyrm::wm
