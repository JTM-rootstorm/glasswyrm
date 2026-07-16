#include "glasswyrmd/grab_state.hpp"

#include "protocol/x11/event_mask.hpp"

#include <algorithm>
#include <bit>
#include <iterator>
#include <limits>
#include <utility>

namespace glasswyrm::server {
namespace {
namespace em = gw::protocol::x11::event_mask;

constexpr std::uint32_t kPointerGrabMask =
    em::ButtonPress | em::ButtonRelease | em::EnterWindow | em::LeaveWindow |
    em::PointerMotion | em::PointerMotionHint | em::Button1Motion |
    em::Button2Motion | em::Button3Motion | em::Button4Motion |
    em::Button5Motion | em::ButtonMotion;
constexpr std::uint32_t kAutomaticGrabMask =
    em::ButtonRelease | em::PointerMotion | em::Button1Motion |
    em::Button2Motion | em::Button3Motion | em::Button4Motion |
    em::Button5Motion | em::ButtonMotion;
constexpr std::uint16_t kCoreModifierMask = 0x00ffU;

bool timestamp_after(const std::uint32_t left,
                     const std::uint32_t right) noexcept {
  const auto difference = left - right;
  return std::bit_cast<std::int32_t>(difference) > 0;
}

bool valid_button(const std::uint8_t button) noexcept {
  return button == kAnyButton || (button >= 1 && button <= 9);
}

bool valid_modifiers(const std::uint16_t modifiers) noexcept {
  return modifiers == kAnyModifier || (modifiers & ~kCoreModifierMask) == 0;
}

std::uint16_t button_bit(const std::uint8_t button) noexcept {
  if (button < 1 || button > 9) return 0;
  return static_cast<std::uint16_t>(1U << (button - 1U));
}

bool mode_is_async(const GrabMode mode) noexcept {
  return mode == GrabMode::Asynchronous;
}

}  // namespace

bool GrabState::valid_time(const std::uint32_t request_time,
                           const std::uint32_t current_time,
                           const std::uint32_t last_grab_time,
                           std::uint32_t& resolved) noexcept {
  resolved = request_time == 0 ? current_time : request_time;
  return !timestamp_after(last_grab_time, resolved) &&
         !timestamp_after(resolved, current_time);
}

GrabStatus GrabState::validate_pointer_fields(
    const std::uint32_t event_mask, const GrabMode pointer_mode,
    const GrabMode keyboard_mode, const std::uint32_t confine_to,
    const std::uint32_t cursor, const bool cursor_valid) noexcept {
  if (!mode_is_async(pointer_mode) || !mode_is_async(keyboard_mode))
    return GrabStatus::BadImplementation;
  if ((event_mask & ~kPointerGrabMask) != 0)
    return GrabStatus::InvalidEventMask;
  if (confine_to != 0) return GrabStatus::UnsupportedConfine;
  if (cursor != 0 && !cursor_valid) return GrabStatus::InvalidCursor;
  return GrabStatus::Success;
}

GrabStatus GrabState::grab_pointer(const PointerGrabRequest& request) noexcept {
  const auto fields = validate_pointer_fields(
      request.event_mask, request.pointer_mode, request.keyboard_mode,
      request.confine_to, request.cursor, request.cursor_valid);
  if (fields != GrabStatus::Success) return fields;
  if (!request.window_viewable) return GrabStatus::NotViewable;
  std::uint32_t activation_time = 0;
  if (!valid_time(request.request_time, request.current_time,
                  last_pointer_grab_time_, activation_time))
    return GrabStatus::InvalidTime;
  if (pointer_grab_.has_value()) return GrabStatus::AlreadyGrabbed;
  pointer_grab_ = PointerGrab{PointerGrabOrigin::Explicit,
                              request.client,
                              request.window,
                              request.owner_events,
                              request.event_mask,
                              request.cursor,
                              request.cursor_image,
                              activation_time,
                              0};
  last_pointer_grab_time_ = activation_time;
  return GrabStatus::Success;
}

bool GrabState::ungrab_pointer(const GrabClientId client,
                               const std::uint32_t request_time,
                               const std::uint32_t current_time) noexcept {
  std::uint32_t resolved = 0;
  if (!pointer_grab_.has_value() || pointer_grab_->client != client ||
      !valid_time(request_time, current_time, last_pointer_grab_time_, resolved))
    return false;
  pointer_grab_.reset();
  return true;
}

GrabStatus GrabState::change_active_pointer_grab(
    const GrabClientId client, const std::uint32_t event_mask,
    const std::uint32_t cursor, const bool cursor_valid,
    std::shared_ptr<const input::CursorImage> cursor_image,
    const std::uint32_t request_time,
    const std::uint32_t current_time) noexcept {
  if (!pointer_grab_.has_value()) return GrabStatus::NotFound;
  if (pointer_grab_->client != client) return GrabStatus::NotOwner;
  if ((event_mask & ~kPointerGrabMask) != 0)
    return GrabStatus::InvalidEventMask;
  if (cursor != 0 && !cursor_valid) return GrabStatus::InvalidCursor;
  std::uint32_t resolved = 0;
  if (!valid_time(request_time, current_time, last_pointer_grab_time_, resolved))
    return GrabStatus::InvalidTime;
  pointer_grab_->event_mask = event_mask;
  pointer_grab_->cursor = cursor;
  pointer_grab_->cursor_image = std::move(cursor_image);
  return GrabStatus::Success;
}

GrabStatus GrabState::grab_keyboard(
    const KeyboardGrabRequest& request) noexcept {
  if (!mode_is_async(request.pointer_mode) ||
      !mode_is_async(request.keyboard_mode))
    return GrabStatus::BadImplementation;
  if (!request.window_viewable) return GrabStatus::NotViewable;
  std::uint32_t activation_time = 0;
  if (!valid_time(request.request_time, request.current_time,
                  last_keyboard_grab_time_, activation_time))
    return GrabStatus::InvalidTime;
  if (keyboard_grab_.has_value()) return GrabStatus::AlreadyGrabbed;
  keyboard_grab_ = KeyboardGrab{request.client, request.window,
                                request.owner_events, activation_time};
  last_keyboard_grab_time_ = activation_time;
  return GrabStatus::Success;
}

bool GrabState::ungrab_keyboard(const GrabClientId client,
                                const std::uint32_t request_time,
                                const std::uint32_t current_time) noexcept {
  std::uint32_t resolved = 0;
  if (!keyboard_grab_.has_value() || keyboard_grab_->client != client ||
      !valid_time(request_time, current_time, last_keyboard_grab_time_, resolved))
    return false;
  keyboard_grab_.reset();
  return true;
}

bool GrabState::begin_automatic_button_grab(
    const GrabClientId client, const std::uint32_t window,
    const std::uint8_t button, const std::uint32_t current_time) noexcept {
  if (pointer_grab_.has_value() || button_bit(button) == 0) return false;
  pointer_grab_ = PointerGrab{PointerGrabOrigin::AutomaticButton,
                              client,
                              window,
                              true,
                              kAutomaticGrabMask,
                              0,
                              nullptr,
                              current_time,
                              button_bit(button)};
  last_pointer_grab_time_ = current_time;
  return true;
}

void GrabState::note_button_press(const std::uint8_t button) noexcept {
  if (!pointer_grab_.has_value() ||
      pointer_grab_->origin == PointerGrabOrigin::Explicit)
    return;
  pointer_grab_->held_buttons |= button_bit(button);
}

bool GrabState::note_button_release(const std::uint8_t button) noexcept {
  if (!pointer_grab_.has_value() ||
      pointer_grab_->origin == PointerGrabOrigin::Explicit)
    return false;
  pointer_grab_->held_buttons &=
      static_cast<std::uint16_t>(~button_bit(button));
  if (pointer_grab_->held_buttons != 0) return false;
  pointer_grab_.reset();
  return true;
}

bool GrabState::passive_overlaps(const PassiveButtonGrabRequest& left,
                                 const PassiveButtonGrabRequest& right) noexcept {
  const bool buttons = left.button == kAnyButton || right.button == kAnyButton ||
                       left.button == right.button;
  const bool modifiers = left.modifiers == kAnyModifier ||
                         right.modifiers == kAnyModifier ||
                         left.modifiers == right.modifiers;
  return left.window == right.window && buttons && modifiers;
}

GrabStatus GrabState::grab_button(const PassiveButtonGrabRequest& request) {
  if (!valid_button(request.button)) return GrabStatus::InvalidButton;
  if (!valid_modifiers(request.modifiers)) return GrabStatus::InvalidModifiers;
  const auto fields = validate_pointer_fields(
      request.event_mask, request.pointer_mode, request.keyboard_mode,
      request.confine_to, request.cursor, request.cursor_valid);
  if (fields != GrabStatus::Success) return fields;

  for (auto& passive : passive_buttons_) {
    if (!passive_overlaps(passive.request, request)) continue;
    if (passive.request.client != request.client) return GrabStatus::BadAccess;
    if (passive.request.button == request.button &&
        passive.request.modifiers == request.modifiers) {
      passive.request = request;
      return GrabStatus::Success;
    }
  }
  passive_buttons_.push_back({request, next_passive_serial_++});
  return GrabStatus::Success;
}

bool GrabState::activate_passive_button(
    const std::uint8_t button, const std::uint16_t modifiers,
    const std::span<const std::uint32_t> pointer_ancestry,
    const std::uint32_t current_time) noexcept {
  if (pointer_grab_.has_value() || button < 1 || button > 9 ||
      pointer_ancestry.empty())
    return false;
  const auto core_modifiers = modifiers & kCoreModifierMask;
  const PassiveButtonGrab* best = nullptr;
  std::size_t best_depth = std::numeric_limits<std::size_t>::max();
  int best_specificity = -1;
  for (const auto& passive : passive_buttons_) {
    const auto ancestor = std::ranges::find(pointer_ancestry,
                                            passive.request.window);
    if (ancestor == pointer_ancestry.end()) continue;
    if (passive.request.button != kAnyButton &&
        passive.request.button != button)
      continue;
    if (passive.request.modifiers != kAnyModifier &&
        passive.request.modifiers != core_modifiers)
      continue;
    const int specificity =
        (passive.request.button != kAnyButton ? 2 : 0) +
        (passive.request.modifiers != kAnyModifier ? 1 : 0);
    const auto depth = static_cast<std::size_t>(
        std::distance(pointer_ancestry.begin(), ancestor));
    if (best == nullptr || depth < best_depth ||
        (depth == best_depth && specificity > best_specificity) ||
        (depth == best_depth && specificity == best_specificity &&
         passive.serial < best->serial)) {
      best = &passive;
      best_depth = depth;
      best_specificity = specificity;
    }
  }
  if (best == nullptr) return false;
  const auto& request = best->request;
  pointer_grab_ = PointerGrab{PointerGrabOrigin::PassiveButton,
                              request.client,
                              request.window,
                              request.owner_events,
                              request.event_mask,
                              request.cursor,
                              request.cursor_image,
                              current_time,
                              button_bit(button)};
  last_pointer_grab_time_ = current_time;
  return true;
}

GrabRouteDecision GrabState::route_pointer(
    const GrabRouteInput& input) const noexcept {
  if (!pointer_grab_.has_value())
    return {GrabRouteKind::Natural, input.natural_client,
            input.natural_window, input.real_pointer_target};
  const auto& grab = *pointer_grab_;
  if (grab.owner_events && input.natural_client == grab.client &&
      input.natural_client != 0)
    return {GrabRouteKind::Natural, input.natural_client,
            input.natural_window, input.real_pointer_target};
  if ((grab.event_mask & input.event_mask) != 0)
    return {GrabRouteKind::GrabWindow, grab.client, grab.window,
            input.real_pointer_target};
  return {GrabRouteKind::Suppressed, 0, 0, input.real_pointer_target};
}

GrabRouteDecision GrabState::route_keyboard(
    const GrabRouteInput& input) const noexcept {
  if (!keyboard_grab_.has_value())
    return {GrabRouteKind::Natural, input.natural_client,
            input.natural_window, input.real_pointer_target};
  const auto& grab = *keyboard_grab_;
  if (grab.owner_events && input.natural_client == grab.client &&
      input.natural_client != 0)
    return {GrabRouteKind::Natural, input.natural_client,
            input.natural_window, input.real_pointer_target};
  return {GrabRouteKind::GrabWindow, grab.client, grab.window,
          input.real_pointer_target};
}

GrabStatus GrabState::allow_events(const AllowEventsMode mode,
                                   const std::uint32_t request_time,
                                   const std::uint32_t current_time) const noexcept {
  if (mode != AllowEventsMode::AsyncPointer &&
      mode != AllowEventsMode::AsyncKeyboard &&
      mode != AllowEventsMode::AsyncBoth)
    return GrabStatus::BadImplementation;
  std::uint32_t last_time = last_pointer_grab_time_;
  if (mode == AllowEventsMode::AsyncKeyboard)
    last_time = last_keyboard_grab_time_;
  else if (mode == AllowEventsMode::AsyncBoth &&
           timestamp_after(last_keyboard_grab_time_, last_pointer_grab_time_))
    last_time = last_keyboard_grab_time_;
  std::uint32_t resolved = 0;
  return valid_time(request_time, current_time, last_time, resolved)
             ? GrabStatus::Success
             : GrabStatus::InvalidTime;
}

GrabCleanupResult GrabState::release_active() noexcept {
  GrabCleanupResult result{pointer_grab_.has_value(),
                           keyboard_grab_.has_value(), 0};
  pointer_grab_.reset();
  keyboard_grab_.reset();
  return result;
}

GrabCleanupResult GrabState::cleanup_client(const GrabClientId client) noexcept {
  GrabCleanupResult result;
  if (pointer_grab_.has_value() && pointer_grab_->client == client) {
    pointer_grab_.reset();
    result.pointer_released = true;
  }
  if (keyboard_grab_.has_value() && keyboard_grab_->client == client) {
    keyboard_grab_.reset();
    result.keyboard_released = true;
  }
  const auto old_size = passive_buttons_.size();
  std::erase_if(passive_buttons_, [client](const PassiveButtonGrab& passive) {
    return passive.request.client == client;
  });
  result.passive_buttons_removed = old_size - passive_buttons_.size();
  return result;
}

GrabCleanupResult GrabState::cleanup_window(const std::uint32_t window) noexcept {
  GrabCleanupResult result;
  if (pointer_grab_.has_value() && pointer_grab_->window == window) {
    pointer_grab_.reset();
    result.pointer_released = true;
  }
  if (keyboard_grab_.has_value() && keyboard_grab_->window == window) {
    keyboard_grab_.reset();
    result.keyboard_released = true;
  }
  const auto old_size = passive_buttons_.size();
  std::erase_if(passive_buttons_, [window](const PassiveButtonGrab& passive) {
    return passive.request.window == window;
  });
  result.passive_buttons_removed = old_size - passive_buttons_.size();
  return result;
}

GrabCleanupResult GrabState::suspend() noexcept { return release_active(); }

}  // namespace glasswyrm::server
