#include "backends/drm/device.hpp"

#include <utility>

namespace glasswyrm::drm {

Device::~Device() { reset(); }

Device::Device(Device &&other) noexcept
    : api_(std::exchange(other.api_, nullptr)),
      handle_(std::exchange(other.handle_, -1)),
      snapshot_(std::move(other.snapshot_)), session_(other.session_) {}

Device &Device::operator=(Device &&other) noexcept {
  if (this == &other)
    return *this;
  reset();
  api_ = std::exchange(other.api_, nullptr);
  handle_ = std::exchange(other.handle_, -1);
  snapshot_ = std::move(other.snapshot_);
  session_ = other.session_;
  return *this;
}

std::optional<Device> Device::open(DrmApi &api, const std::string_view path,
                                   const DeviceOpenOptions &options,
                                   DeviceDiscovery &discovery) {
  return finish_open(api, api.open_device(path, options),
                     DeviceSession::Standalone, discovery);
}

std::optional<Device> Device::adopt(DrmApi &api, const int inherited_fd,
                                    const DeviceOpenOptions &options,
                                    DeviceDiscovery &discovery) {
  return finish_open(api, api.adopt_device(inherited_fd, options),
                     DeviceSession::External, discovery);
}

std::optional<Device> Device::finish_open(DrmApi &api, DeviceOpenResult result,
                                          const DeviceSession session,
                                          DeviceDiscovery &discovery) {
  discovery.status = result.status;
  discovery.error = std::move(result.error);
  if (result.status != DeviceOpenStatus::Success) {
    if (result.handle >= 0)
      api.close_device(result.handle);
    return std::nullopt;
  }

  auto reject = [&](const DeviceOpenStatus status, std::string error) {
    api.close_device(result.handle);
    discovery.status = status;
    discovery.error = std::move(error);
    return std::optional<Device>{};
  };
  if (result.handle < 0)
    return reject(DeviceOpenStatus::OpenFailed,
                  "DRM API returned success without an open device");
  if (!result.snapshot.primary_node)
    return reject(DeviceOpenStatus::NotPrimaryNode,
                  "DRM device is not a primary node");
  if (!result.snapshot.dumb_buffer)
    return reject(DeviceOpenStatus::MissingDumbBuffer,
                  "DRM device does not support dumb buffers");
  if (result.snapshot.crtcs.empty() || result.snapshot.connectors.empty())
    return reject(DeviceOpenStatus::MissingResources,
                  "DRM device has no usable CRTC or connector resources");

  discovery.error.clear();
  return Device(api, result.handle, std::move(result.snapshot), session);
}

int Device::poll_fd() const noexcept {
  return valid() ? api_->poll_fd(handle_) : -1;
}

int Device::duplicate_fd(std::string &error) const {
  if (!valid()) {
    error = "DRM device is not open";
    return -1;
  }
  return api_->duplicate_fd(handle_, error);
}

bool Device::arm_page_flip(const std::shared_ptr<PageFlipCookie> &cookie,
                           std::string &error) {
  if (!valid()) {
    error = "DRM device is not open";
    return false;
  }
  return api_->arm_page_flip(handle_, cookie, error);
}

void Device::cancel_page_flip(
    const std::shared_ptr<PageFlipCookie> &cookie) noexcept {
  if (valid())
    api_->cancel_page_flip(handle_, cookie);
}

void Device::abandon_page_flip(
    const std::shared_ptr<PageFlipCookie> &cookie) noexcept {
  if (valid())
    api_->abandon_page_flip(handle_, cookie);
}

DrmEvent Device::service_events(const short revents) {
  if (!valid())
    return {DrmEventKind::Error, 0, 0, 0, "DRM device is not open"};
  return api_->service_events(handle_, revents);
}

void Device::reset() noexcept {
  if (valid())
    api_->close_device(handle_);
  api_ = nullptr;
  handle_ = -1;
  snapshot_ = {};
  session_ = DeviceSession::Standalone;
}

} // namespace glasswyrm::drm
