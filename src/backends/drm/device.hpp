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

enum class DeviceSession { Standalone, External };

class Device {
public:
  Device() = default;
  ~Device();
  Device(const Device &) = delete;
  Device &operator=(const Device &) = delete;
  Device(Device &&other) noexcept;
  Device &operator=(Device &&other) noexcept;

  [[nodiscard]] static std::optional<Device>
  open(DrmApi &api, std::string_view path, const DeviceOpenOptions &options,
       DeviceDiscovery &discovery);
  [[nodiscard]] static std::optional<Device>
  adopt(DrmApi &api, int inherited_fd, const DeviceOpenOptions &options,
        DeviceDiscovery &discovery);

  [[nodiscard]] bool valid() const noexcept { return handle_ >= 0; }
  [[nodiscard]] const DeviceSnapshot &snapshot() const noexcept {
    return snapshot_;
  }
  [[nodiscard]] int poll_fd() const noexcept;
  // Borrowed by KmsApi and event consumers. The Device remains the sole owner.
  [[nodiscard]] int borrowed_kms_fd() const noexcept { return handle_; }
  [[nodiscard]] DeviceSession session() const noexcept { return session_; }
  [[nodiscard]] bool may_manage_master() const noexcept {
    return session_ == DeviceSession::Standalone;
  }
  [[nodiscard]] int duplicate_fd(std::string &error) const;
  [[nodiscard]] bool
  arm_page_flip(const std::shared_ptr<PageFlipCookie> &cookie,
                std::string &error);
  void cancel_page_flip(const std::shared_ptr<PageFlipCookie> &cookie) noexcept;
  void
  abandon_page_flip(const std::shared_ptr<PageFlipCookie> &cookie) noexcept;
  [[nodiscard]] DrmEvent service_events(short revents);
  void reset() noexcept;

private:
  Device(DrmApi &api, int handle, DeviceSnapshot snapshot,
         DeviceSession session)
      : api_(&api), handle_(handle), snapshot_(std::move(snapshot)),
        session_(session) {}

  [[nodiscard]] static std::optional<Device>
  finish_open(DrmApi &api, DeviceOpenResult result, DeviceSession session,
              DeviceDiscovery &discovery);

  DrmApi *api_{};
  int handle_{-1};
  DeviceSnapshot snapshot_;
  DeviceSession session_{DeviceSession::Standalone};
};

} // namespace glasswyrm::drm
