#include "input/libinput_api.hpp"

#include "input/device_path_allowlist.hpp"

#include <libinput.h>

#include <map>
#include <memory>
#include <optional>

namespace glasswyrm::input {
namespace {

class RealLibinputApi final : public LibinputApi {
 public:
  ~RealLibinputApi() override {
    if (context_ != nullptr) libinput_unref(context_);
  }

  bool create(DevicePathAllowlist &access,
              std::span<const std::string> paths,
              std::string &error) override {
    if (context_ != nullptr) {
      error = "libinput context already exists";
      return false;
    }
    access_ = &access;
    context_ = libinput_path_create_context(&kInterface, access_);
    if (context_ == nullptr) {
      error = "libinput path context creation failed";
      return false;
    }
    for (const auto &path : paths) {
      if (libinput_path_add_device(context_, path.c_str()) == nullptr) {
        error = "libinput rejected configured device " + path;
        libinput_unref(context_);
        context_ = nullptr;
        access_ = nullptr;
        return false;
      }
    }
    error.clear();
    return true;
  }

  int poll_fd() const noexcept override {
    return context_ == nullptr ? -1 : libinput_get_fd(context_);
  }

  bool dispatch(std::string &error) override {
    if (context_ == nullptr || libinput_dispatch(context_) != 0) {
      error = "libinput dispatch failed";
      return false;
    }
    error.clear();
    return true;
  }

  bool next_event(std::uint32_t root_width, std::uint32_t root_height,
                  LibinputEvent &out) override {
    if (pending_) {
      out = std::move(*pending_);
      pending_.reset();
      return true;
    }
    if (context_ == nullptr) return false;
    libinput_event *event = libinput_get_event(context_);
    if (event == nullptr) return false;
    const auto type = libinput_event_get_type(event);
    libinput_device *device = libinput_event_get_device(event);
    const auto id = device_id(device);
    out = {};
    out.device_id = id;
    if (type == LIBINPUT_EVENT_DEVICE_ADDED ||
        type == LIBINPUT_EVENT_DEVICE_REMOVED) {
      out.kind = type == LIBINPUT_EVENT_DEVICE_ADDED
                     ? LibinputEventKind::DeviceAdded
                     : LibinputEventKind::DeviceRemoved;
      if (libinput_device_has_capability(device,
                                         LIBINPUT_DEVICE_CAP_KEYBOARD))
        out.capabilities |= DeviceCapabilityKeyboard;
      if (libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_POINTER))
        out.capabilities |= DeviceCapabilityPointer;
    } else if (type == LIBINPUT_EVENT_KEYBOARD_KEY) {
      auto *key = libinput_event_get_keyboard_event(event);
      out.kind = LibinputEventKind::Key;
      out.time_usec = libinput_event_keyboard_get_time_usec(key);
      out.code = libinput_event_keyboard_get_key(key);
      out.pressed = libinput_event_keyboard_get_key_state(key) ==
                    LIBINPUT_KEY_STATE_PRESSED;
    } else if (type == LIBINPUT_EVENT_POINTER_MOTION ||
               type == LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE ||
               type == LIBINPUT_EVENT_POINTER_BUTTON ||
               type == LIBINPUT_EVENT_POINTER_SCROLL_WHEEL ||
               type == LIBINPUT_EVENT_POINTER_SCROLL_FINGER ||
               type == LIBINPUT_EVENT_POINTER_SCROLL_CONTINUOUS ||
               type == LIBINPUT_EVENT_POINTER_AXIS) {
      auto *pointer = libinput_event_get_pointer_event(event);
      out.time_usec = libinput_event_pointer_get_time_usec(pointer);
      if (type == LIBINPUT_EVENT_POINTER_MOTION) {
        out.kind = LibinputEventKind::MotionRelative;
        // Pointer acceleration policy is deferred beyond M11. Consume the
        // device's unaccelerated relative deltas so routing remains stable
        // and does not silently inherit libinput's desktop profile.
        out.x = libinput_event_pointer_get_dx_unaccelerated(pointer);
        out.y = libinput_event_pointer_get_dy_unaccelerated(pointer);
      } else if (type == LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE) {
        out.kind = LibinputEventKind::MotionAbsolute;
        out.x = libinput_event_pointer_get_absolute_x_transformed(pointer,
                                                                   root_width);
        out.y = libinput_event_pointer_get_absolute_y_transformed(pointer,
                                                                   root_height);
      } else if (type == LIBINPUT_EVENT_POINTER_BUTTON) {
        out.kind = LibinputEventKind::Button;
        out.code = libinput_event_pointer_get_button(pointer);
        out.pressed = libinput_event_pointer_get_button_state(pointer) ==
                      LIBINPUT_BUTTON_STATE_PRESSED;
      } else if (type == LIBINPUT_EVENT_POINTER_SCROLL_WHEEL) {
        out.kind = LibinputEventKind::Wheel;
        const bool vertical = libinput_event_pointer_has_axis(
            pointer, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
        const bool horizontal = libinput_event_pointer_has_axis(
            pointer, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
        if (vertical) {
          out.scroll_axis = ScrollAxis::Vertical;
          out.wheel_v120 = libinput_event_pointer_get_scroll_value_v120(
              pointer, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
          if (horizontal) {
            pending_ = out;
            pending_->scroll_axis = ScrollAxis::Horizontal;
            pending_->wheel_v120 = libinput_event_pointer_get_scroll_value_v120(
                pointer, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
          }
        } else if (horizontal) {
          out.scroll_axis = ScrollAxis::Horizontal;
          out.wheel_v120 = libinput_event_pointer_get_scroll_value_v120(
              pointer, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
        }
      } else if (type == LIBINPUT_EVENT_POINTER_SCROLL_FINGER) {
        out.kind = LibinputEventKind::FingerScroll;
      } else if (type == LIBINPUT_EVENT_POINTER_SCROLL_CONTINUOUS) {
        out.kind = LibinputEventKind::ContinuousScroll;
      } else {
        out.kind = LibinputEventKind::DeprecatedAxis;
      }
    } else {
      out.kind = LibinputEventKind::Unsupported;
    }
    if (type == LIBINPUT_EVENT_DEVICE_REMOVED) ids_.erase(device);
    libinput_event_destroy(event);
    return true;
  }

  bool suspend(std::string &error) override {
    if (context_ == nullptr) {
      error = "libinput suspend failed";
      return false;
    }
    libinput_suspend(context_);
    error.clear();
    return true;
  }

  bool resume(std::string &error) override {
    if (context_ == nullptr || libinput_resume(context_) != 0) {
      error = "libinput resume failed";
      return false;
    }
    error.clear();
    return true;
  }

 private:
  static int open_restricted(const char *path, int flags, void *data) {
    return static_cast<DevicePathAllowlist *>(data)->open_restricted(path,
                                                                     flags);
  }
  static void close_restricted(int fd, void *data) {
    static_cast<DevicePathAllowlist *>(data)->close_restricted(fd);
  }
  std::uint64_t device_id(libinput_device *device) {
    const auto found = ids_.find(device);
    if (found != ids_.end()) return found->second;
    const auto id = next_id_++;
    ids_.emplace(device, id);
    return id;
  }

  static constexpr libinput_interface kInterface{open_restricted,
                                                  close_restricted};
  libinput *context_{};
  DevicePathAllowlist *access_{};
  std::map<libinput_device *, std::uint64_t> ids_;
  std::optional<LibinputEvent> pending_;
  std::uint64_t next_id_{1};
};

}  // namespace

std::unique_ptr<LibinputApi> make_real_libinput_api() {
  return std::make_unique<RealLibinputApi>();
}

}  // namespace glasswyrm::input
