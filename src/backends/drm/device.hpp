#pragma once

#include "backends/drm/drm_api.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace glasswyrm::drm {

struct DeviceDiscovery {
  DeviceOpenStatus status{DeviceOpenStatus::OpenFailed};
  std::string error;
};

class Device {
 public:
  Device() = default;
  ~Device();
  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;
  Device(Device&& other) noexcept;
  Device& operator=(Device&& other) noexcept;

  [[nodiscard]] static std::optional<Device> open(
      DrmApi& api, std::string_view path, const DeviceOpenOptions& options,
      DeviceDiscovery& discovery);

  [[nodiscard]] bool valid() const noexcept { return handle_ >= 0; }
  [[nodiscard]] const DeviceSnapshot& snapshot() const noexcept {
    return snapshot_;
  }
  [[nodiscard]] int poll_fd() const noexcept;
  [[nodiscard]] int duplicate_fd(std::string& error) const;
  [[nodiscard]] bool arm_page_flip(PageFlipCookie& cookie,
                                   std::string& error);
  void disarm_page_flip(PageFlipCookie& cookie) noexcept;
  [[nodiscard]] DrmEvent service_events(short revents);
  void reset() noexcept;

 private:
  Device(DrmApi& api, int handle, DeviceSnapshot snapshot)
      : api_(&api), handle_(handle), snapshot_(std::move(snapshot)) {}

  DrmApi* api_{};
  int handle_{-1};
  DeviceSnapshot snapshot_;
};

}  // namespace glasswyrm::drm
