#include "backends/drm/fake_drm_api.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <unistd.h>
#include <utility>

namespace glasswyrm::drm {

FakeDrmApi::FakeDrmApi(FakeDeviceConfig config) : config_(std::move(config)) {
  if (::pipe2(event_pipe_, O_NONBLOCK | O_CLOEXEC) != 0)
    throw std::runtime_error(std::string("fake DRM event pipe failed: ") +
                             std::strerror(errno));
}

FakeDrmApi::~FakeDrmApi() {
  if (open_ && owns_active_handle_)
    (void)::close(active_handle_);
  (void)::close(event_pipe_[0]);
  (void)::close(event_pipe_[1]);
}

DeviceOpenResult FakeDrmApi::open_device(const std::string_view path,
                                         const DeviceOpenOptions &options) {
  last_options_ = options;
  if (path != config_.accepted_path) {
    return {DeviceOpenStatus::InvalidPath,
            -1,
            {},
            "fake DRM device path was not configured"};
  }
  if (config_.open_status != DeviceOpenStatus::Success)
    return {config_.open_status, -1, config_.snapshot, config_.error};
  if (open_)
    return {DeviceOpenStatus::OpenFailed,
            -1,
            {},
            "fake DRM device is already open"};
  open_ = true;
  owns_active_handle_ = false;
  active_handle_ = kHandle;
  auto snapshot = config_.snapshot;
  snapshot.universal_planes =
      options.request_universal_planes && snapshot.universal_planes;
  snapshot.atomic = options.request_atomic && snapshot.atomic;
  return {DeviceOpenStatus::Success, kHandle, std::move(snapshot), {}};
}

DeviceOpenResult FakeDrmApi::adopt_device(const int inherited_fd,
                                          const DeviceOpenOptions &options) {
  last_options_ = options;
  if (config_.open_status != DeviceOpenStatus::Success)
    return {config_.open_status, -1, config_.snapshot, config_.error};
  if (open_)
    return {DeviceOpenStatus::OpenFailed,
            -1,
            {},
            "fake DRM device is already open"};
  const int duplicate = ::fcntl(inherited_fd, F_DUPFD_CLOEXEC, 0);
  if (duplicate < 0)
    return {DeviceOpenStatus::OpenFailed,
            -1,
            {},
            std::string("fake inherited DRM FD duplication failed: ") +
                std::strerror(errno)};
  open_ = true;
  owns_active_handle_ = true;
  active_handle_ = duplicate;
  last_adopted_handle_ = duplicate;
  auto snapshot = config_.snapshot;
  snapshot.universal_planes =
      options.request_universal_planes && snapshot.universal_planes;
  snapshot.atomic = options.request_atomic && snapshot.atomic;
  return {DeviceOpenStatus::Success, duplicate, std::move(snapshot), {}};
}

void FakeDrmApi::close_device(const int handle) noexcept {
  if (handle != active_handle_ || !open_)
    return;
  armed_cookie_ = nullptr;
  if (owns_active_handle_)
    (void)::close(active_handle_);
  last_closed_handle_ = active_handle_;
  open_ = false;
  owns_active_handle_ = false;
  active_handle_ = -1;
  ++close_count_;
}

int FakeDrmApi::poll_fd(const int handle) const noexcept {
  return handle == active_handle_ && open_ ? event_pipe_[0] : -1;
}

int FakeDrmApi::duplicate_fd(const int handle, std::string &error) {
  if (handle != active_handle_ || !open_) {
    error = "fake DRM device is not open";
    return -1;
  }
  const int duplicate = ::fcntl(event_pipe_[0], F_DUPFD_CLOEXEC, 0);
  if (duplicate < 0) {
    error =
        std::string("fake DRM FD duplication failed: ") + std::strerror(errno);
    return -1;
  }
  error.clear();
  return duplicate;
}

bool FakeDrmApi::arm_page_flip(const int handle, PageFlipCookie &cookie,
                               std::string &error) {
  if (handle != active_handle_ || !open_) {
    error = "fake DRM device is not open";
    return false;
  }
  if (armed_cookie_) {
    error = "fake DRM page flip is already armed";
    return false;
  }
  cookie.completed = false;
  cookie.completed_crtc_id = 0;
  cookie.completed_sequence = 0;
  armed_cookie_ = &cookie;
  error.clear();
  return true;
}

void FakeDrmApi::disarm_page_flip(const int handle,
                                  PageFlipCookie &cookie) noexcept {
  if (handle == active_handle_ && armed_cookie_ == &cookie)
    armed_cookie_ = nullptr;
}

DrmEvent FakeDrmApi::service_events(const int handle, const short revents) {
  if (handle != active_handle_ || !open_)
    return {DrmEventKind::Error, 0, 0, 0, "fake DRM device is not open"};
  if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
    return {DrmEventKind::Error, 0, 0, 0,
            "fake DRM event FD reported an error"};
  if ((revents & POLLIN) == 0 || events_.empty())
    return {};

  std::uint8_t byte{};
  if (::read(event_pipe_[0], &byte, sizeof(byte)) < 0 && errno != EAGAIN)
    return {DrmEventKind::Error, 0, 0, 0,
            std::string("fake DRM event read failed: ") + std::strerror(errno)};
  auto event = std::move(events_.front());
  events_.pop_front();
  if (event.kind == DrmEventKind::PageFlip && armed_cookie_) {
    armed_cookie_->completed_crtc_id = event.crtc_id;
    armed_cookie_->completed_sequence = event.sequence;
    armed_cookie_->completed = true;
    event.token = armed_cookie_->token;
    armed_cookie_ = nullptr;
  }
  return event;
}

void FakeDrmApi::queue_page_flip(const std::uint64_t token,
                                 const std::uint32_t crtc_id,
                                 const std::uint32_t sequence) {
  events_.push_back({DrmEventKind::PageFlip, token, crtc_id, sequence, {}});
  signal_event();
}

void FakeDrmApi::queue_error(std::string error) {
  events_.push_back({DrmEventKind::Error, 0, 0, 0, std::move(error)});
  signal_event();
}

void FakeDrmApi::signal_event() {
  const std::uint8_t byte = 1;
  if (::write(event_pipe_[1], &byte, sizeof(byte)) < 0 && errno != EAGAIN)
    throw std::runtime_error(std::string("fake DRM event signal failed: ") +
                             std::strerror(errno));
}

} // namespace glasswyrm::drm
