#include "input/libinput_backend.hpp"

#include <linux/input-event-codes.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace glasswyrm::input {
namespace {

bool has_capability(const std::uint8_t capabilities,
                    const DeviceCapability capability) {
  return (capabilities & static_cast<std::uint8_t>(capability)) != 0;
}

std::optional<std::uint32_t> x_button(const std::uint32_t evdev_button) {
  switch (evdev_button) {
    case BTN_LEFT: return 1;
    case BTN_MIDDLE: return 2;
    case BTN_RIGHT: return 3;
    case BTN_SIDE: return 8;
    case BTN_EXTRA: return 9;
    default: return std::nullopt;
  }
}

std::int32_t clamp_coordinate(const std::int64_t value,
                              const std::uint32_t extent) {
  const auto maximum = extent == 0
                           ? 0
                           : static_cast<std::int64_t>(std::min<std::uint32_t>(
                                 extent - 1,
                                 std::numeric_limits<std::int32_t>::max()));
  return static_cast<std::int32_t>(std::clamp<std::int64_t>(value, 0, maximum));
}

}  // namespace

std::uint32_t LibinputTimestampConverter::convert(
    const std::uint64_t time_usec) noexcept {
  const auto monotonic_usec = std::max(time_usec, last_usec_);
  auto milliseconds =
      static_cast<std::uint32_t>((monotonic_usec / 1000U) & 0xffffffffU);
  if (milliseconds == 0) milliseconds = 1;
  last_usec_ = monotonic_usec;
  last_ms_ = milliseconds;
  return last_ms_;
}

bool LibinputBackend::initialize(
    const std::span<const std::string> device_paths,
    const std::uint32_t root_width, const std::uint32_t root_height,
    std::string &error) {
  if (initialized_) {
    error = "libinput backend is already initialized";
    return false;
  }
  if (root_width == 0 || root_height == 0) {
    error = "libinput root dimensions must be nonzero";
    return false;
  }
  access_ = DevicePathAllowlist::create(device_paths, error);
  if (!access_) return false;
  root_width_ = root_width;
  root_height_ = root_height;
  if (!api_.create(*access_, access_->paths(), error)) {
    access_.reset();
    return false;
  }
  initialized_ = true;
  active_ = true;
  if (!api_.dispatch(error)) {
    initialized_ = false;
    active_ = false;
    return false;
  }
  auto initial = drain({256, 256}, false);
  if (initial.status == InputServiceStatus::Fatal ||
      initial.status == InputServiceStatus::BudgetExhausted ||
      !readiness().ready()) {
    error = initial.error.empty()
                ? "libinput requires keyboard and pointer capabilities"
                : initial.error;
    initialized_ = false;
    active_ = false;
    return false;
  }
  error.clear();
  return true;
}

InputServiceResult LibinputBackend::service(const InputDrainBudget budget) {
  if (!initialized_ || !active_)
    return {InputServiceStatus::Inactive, {}, 0, 0, {}};
  std::string error;
  if (!api_.dispatch(error))
    return {InputServiceStatus::Fatal, {}, 0, 0, std::move(error)};
  return drain(budget, true);
}

bool LibinputBackend::suspend(std::string &error) {
  if (!initialized_) {
    error = "libinput backend is not initialized";
    return false;
  }
  if (!active_) {
    error.clear();
    return true;
  }
  clear_provider_state();
  if (!api_.suspend(error)) return false;
  active_ = false;
  error.clear();
  return true;
}

bool LibinputBackend::resume(std::string &error) {
  if (!initialized_) {
    error = "libinput backend is not initialized";
    return false;
  }
  if (active_) {
    error.clear();
    return true;
  }
  devices_.clear();
  relative_x_ = 0.0;
  relative_y_ = 0.0;
  if (!api_.resume(error) || !api_.dispatch(error)) return false;
  active_ = true;
  auto initial = drain({256, 256}, false);
  if (initial.status != InputServiceStatus::Complete || !readiness().ready()) {
    active_ = false;
    error = initial.error.empty()
                ? "libinput resume did not restore keyboard and pointer"
                : initial.error;
    return false;
  }
  error.clear();
  return true;
}

InputReadiness LibinputBackend::readiness() const noexcept {
  InputReadiness result;
  for (const auto &[id, device] : devices_) {
    (void)id;
    result.keyboard |=
        has_capability(device.capabilities, DeviceCapabilityKeyboard);
    result.pointer |=
        has_capability(device.capabilities, DeviceCapabilityPointer) ||
        has_capability(device.capabilities, DeviceCapabilityAbsolutePointer);
  }
  return result;
}

std::size_t LibinputBackend::held_key_count() const noexcept {
  std::size_t count = 0;
  for (const auto &[id, device] : devices_) {
    (void)id;
    count += device.held_keys.size();
  }
  return count;
}

std::size_t LibinputBackend::held_button_count() const noexcept {
  std::size_t count = 0;
  for (const auto &[id, device] : devices_) {
    (void)id;
    count += device.held_buttons.size();
  }
  return count;
}

InputServiceResult LibinputBackend::drain(const InputDrainBudget budget,
                                          const bool publish_records) {
  if (budget.maximum_events == 0 || budget.maximum_work_units == 0)
    return {InputServiceStatus::BudgetExhausted, {}, 0, 0, {}};
  InputServiceResult result;
  LibinputEvent event;
  const auto limit = std::min(budget.maximum_events, budget.maximum_work_units);
  while (result.consumed_events < limit &&
         api_.next_event(root_width_, root_height_, event)) {
    ++result.consumed_events;
    convert(event, result, publish_records);
  }
  if (result.consumed_events == limit)
    result.status = InputServiceStatus::BudgetExhausted;
  return result;
}

void LibinputBackend::warp_pointer(const std::int32_t x,
                                   const std::int32_t y) noexcept {
  pointer_x_ = clamp_coordinate(x, root_width_);
  pointer_y_ = clamp_coordinate(y, root_height_);
  relative_x_ = 0;
  relative_y_ = 0;
}

bool LibinputBackend::update_root_bounds(const std::uint32_t width,
                                         const std::uint32_t height) noexcept {
  if (!can_update_root_bounds(width, height))
    return false;
  root_width_ = width;
  root_height_ = height;
  warp_pointer(pointer_x_, pointer_y_);
  return true;
}

void LibinputBackend::convert(const LibinputEvent &event,
                              InputServiceResult &result,
                              const bool publish_records) {
  if (event.kind == LibinputEventKind::DeviceAdded) {
    devices_[event.device_id].capabilities = event.capabilities;
    return;
  }
  if (event.kind == LibinputEventKind::DeviceRemoved) {
    devices_.erase(event.device_id);
    result.provider_state_reset = true;
    if (!readiness().ready()) note_ignored(result);
    return;
  }
  auto found = devices_.find(event.device_id);
  if (found == devices_.end()) {
    note_ignored(result);
    return;
  }
  auto &device = found->second;
  const auto time_ms = timestamp_.convert(event.time_usec);
  if (event.kind == LibinputEventKind::MotionRelative) {
    if (!has_capability(device.capabilities, DeviceCapabilityPointer)) {
      note_ignored(result);
      return;
    }
    relative_x_ += event.x;
    relative_y_ += event.y;
    const auto whole_x = static_cast<std::int64_t>(std::trunc(relative_x_));
    const auto whole_y = static_cast<std::int64_t>(std::trunc(relative_y_));
    relative_x_ -= static_cast<double>(whole_x);
    relative_y_ -= static_cast<double>(whole_y);
    if (whole_x == 0 && whole_y == 0) return;
    pointer_x_ = clamp_coordinate(static_cast<std::int64_t>(pointer_x_) + whole_x,
                                  root_width_);
    pointer_y_ = clamp_coordinate(static_cast<std::int64_t>(pointer_y_) + whole_y,
                                  root_height_);
    if (publish_records)
      result.records.push_back({RealInputKind::MotionRelative, event.device_id,
                                time_ms, pointer_x_, pointer_y_, 0, false});
    return;
  }
  if (event.kind == LibinputEventKind::MotionAbsolute) {
    if (!has_capability(device.capabilities, DeviceCapabilityAbsolutePointer) &&
        !has_capability(device.capabilities, DeviceCapabilityPointer)) {
      note_ignored(result);
      return;
    }
    pointer_x_ = clamp_coordinate(static_cast<std::int64_t>(event.x), root_width_);
    pointer_y_ = clamp_coordinate(static_cast<std::int64_t>(event.y), root_height_);
    if (publish_records)
      result.records.push_back({RealInputKind::MotionAbsolute, event.device_id,
                                time_ms, pointer_x_, pointer_y_, 0, false});
    return;
  }
  if (event.kind == LibinputEventKind::Button) {
    const auto mapped = x_button(event.code);
    if (!mapped ||
        !has_capability(device.capabilities, DeviceCapabilityPointer)) {
      note_ignored(result);
      return;
    }
    if (event.pressed) device.held_buttons.insert(*mapped);
    else device.held_buttons.erase(*mapped);
    add_button_record(event, *mapped, event.pressed, result, publish_records);
    return;
  }
  if (event.kind == LibinputEventKind::Wheel) {
    if (!has_capability(device.capabilities, DeviceCapabilityPointer)) {
      note_ignored(result);
      return;
    }
    auto &remainder = event.scroll_axis == ScrollAxis::Vertical
                          ? device.vertical_v120
                          : device.horizontal_v120;
    remainder += event.wheel_v120;
    while (remainder <= -120.0 || remainder >= 120.0) {
      const bool negative = remainder < 0.0;
      const std::uint32_t button = event.scroll_axis == ScrollAxis::Vertical
                                       ? (negative ? 4U : 5U)
                                       : (negative ? 6U : 7U);
      add_button_record(event, button, true, result, publish_records);
      add_button_record(event, button, false, result, publish_records);
      remainder += negative ? 120.0 : -120.0;
    }
    return;
  }
  if (event.kind == LibinputEventKind::Key) {
    if (!has_capability(device.capabilities, DeviceCapabilityKeyboard)) {
      note_ignored(result);
      return;
    }
    if (event.pressed) device.held_keys.insert(event.code);
    else device.held_keys.erase(event.code);
    if (publish_records)
      result.records.push_back({RealInputKind::Key, event.device_id, time_ms,
                                pointer_x_, pointer_y_, event.code,
                                event.pressed});
    return;
  }
  note_ignored(result);
}

void LibinputBackend::add_button_record(const LibinputEvent &event,
                                        const std::uint32_t button,
                                        const bool pressed,
                                        InputServiceResult &result,
                                        const bool publish_records) {
  if (!publish_records) return;
  result.records.push_back(
      {RealInputKind::Button, event.device_id,
       timestamp_.convert(event.time_usec), pointer_x_, pointer_y_, button,
       pressed});
}

void LibinputBackend::clear_provider_state() noexcept {
  for (auto &[id, device] : devices_) {
    (void)id;
    device.held_keys.clear();
    device.held_buttons.clear();
    device.vertical_v120 = 0.0;
    device.horizontal_v120 = 0.0;
  }
  relative_x_ = 0.0;
  relative_y_ = 0.0;
}

void LibinputBackend::note_ignored(InputServiceResult &result) noexcept {
  ++result.ignored_events;
  if (bounded_diagnostics_ < kMaximumDiagnostics) ++bounded_diagnostics_;
}

}  // namespace glasswyrm::input
