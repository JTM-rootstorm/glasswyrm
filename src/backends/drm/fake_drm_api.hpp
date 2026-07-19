#pragma once

#include "backends/drm/drm_api.hpp"

#include <deque>
#include <optional>
#include <string>
#include <string_view>

namespace glasswyrm::drm {

struct FakeDeviceConfig {
  std::string accepted_path{"/dev/dri/card0"};
  DeviceOpenStatus open_status{DeviceOpenStatus::Success};
  DeviceSnapshot snapshot;
  std::string error;
};

class FakeDrmApi final : public DrmApi {
public:
  explicit FakeDrmApi(FakeDeviceConfig config);
  ~FakeDrmApi() override;

  [[nodiscard]] DeviceOpenResult
  open_device(std::string_view path, const DeviceOpenOptions &options) override;
  [[nodiscard]] DeviceOpenResult
  adopt_device(int inherited_fd, const DeviceOpenOptions &options) override;
  void close_device(int handle) noexcept override;
  [[nodiscard]] int poll_fd(int handle) const noexcept override;
  [[nodiscard]] int duplicate_fd(int handle, std::string &error) override;
  [[nodiscard]] bool
  arm_page_flip(int handle, const std::shared_ptr<PageFlipCookie> &cookie,
                std::string &error) override;
  void cancel_page_flip(
      int handle,
      const std::shared_ptr<PageFlipCookie> &cookie) noexcept override;
  void abandon_page_flip(
      int handle,
      const std::shared_ptr<PageFlipCookie> &cookie) noexcept override;
  [[nodiscard]] DrmEvent service_events(int handle, short revents) override;

  void queue_page_flip(std::uint64_t token, std::uint32_t crtc_id,
                       std::uint32_t sequence,
                       std::uint64_t kernel_timestamp_nanoseconds = 0,
                       bool timestamp_available = false);
  void queue_error(std::string error);

  [[nodiscard]] bool open() const noexcept { return open_; }
  [[nodiscard]] std::size_t close_count() const noexcept {
    return close_count_;
  }
  [[nodiscard]] const DeviceOpenOptions &last_options() const noexcept {
    return last_options_;
  }
  [[nodiscard]] int last_adopted_handle() const noexcept {
    return last_adopted_handle_;
  }
  [[nodiscard]] int last_closed_handle() const noexcept {
    return last_closed_handle_;
  }

private:
  void signal_event();

  static constexpr int kHandle = 1;
  FakeDeviceConfig config_;
  int event_pipe_[2]{-1, -1};
  bool open_{};
  bool owns_active_handle_{};
  int active_handle_{-1};
  int last_adopted_handle_{-1};
  int last_closed_handle_{-1};
  std::size_t close_count_{};
  DeviceOpenOptions last_options_;
  std::shared_ptr<PageFlipCookie> event_cookie_;
  bool event_cookie_armed_{};
  std::deque<DrmEvent> events_;
  std::optional<std::uint64_t> last_page_flip_timestamp_;
};

} // namespace glasswyrm::drm
