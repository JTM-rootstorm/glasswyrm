#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace glasswyrm::input {

class DevicePathAllowlist;

enum class LibinputEventKind {
  DeviceAdded,
  DeviceRemoved,
  Key,
  MotionRelative,
  MotionAbsolute,
  Button,
  Wheel,
  FingerScroll,
  ContinuousScroll,
  DeprecatedAxis,
  Unsupported,
};

enum class ScrollAxis { Vertical, Horizontal };

enum DeviceCapability : std::uint8_t {
  DeviceCapabilityNone = 0,
  DeviceCapabilityKeyboard = 1U << 0U,
  DeviceCapabilityPointer = 1U << 1U,
  DeviceCapabilityAbsolutePointer = 1U << 2U,
};

struct LibinputEvent {
  LibinputEventKind kind{LibinputEventKind::Unsupported};
  std::uint64_t device_id{};
  std::uint64_t time_usec{};
  std::uint32_t code{};
  bool pressed{};
  std::uint8_t capabilities{};
  double x{};
  double y{};
  double wheel_v120{};
  ScrollAxis scroll_axis{ScrollAxis::Vertical};
};

class LibinputApi {
 public:
  virtual ~LibinputApi() = default;

  [[nodiscard]] virtual bool create(
      DevicePathAllowlist &access, std::span<const std::string> paths,
      std::string &error) = 0;
  [[nodiscard]] virtual int poll_fd() const noexcept = 0;
  [[nodiscard]] virtual bool dispatch(std::string &error) = 0;
  [[nodiscard]] virtual bool next_event(std::uint32_t root_width,
                                        std::uint32_t root_height,
                                        LibinputEvent &event) = 0;
  [[nodiscard]] virtual bool suspend(std::string &error) = 0;
  [[nodiscard]] virtual bool resume(std::string &error) = 0;
};

[[nodiscard]] std::unique_ptr<LibinputApi> make_real_libinput_api();

}  // namespace glasswyrm::input
