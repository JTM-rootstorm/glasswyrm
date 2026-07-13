#pragma once

#include "backends/drm/drm_api.hpp"

#include <deque>
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

  [[nodiscard]] DeviceOpenResult open_device(
      std::string_view path, const DeviceOpenOptions& options) override;
  void close_device(int handle) noexcept override;
  [[nodiscard]] int poll_fd(int handle) const noexcept override;
  [[nodiscard]] int duplicate_fd(int handle, std::string& error) override;
  [[nodiscard]] bool arm_page_flip(int handle, PageFlipCookie& cookie,
                                   std::string& error) override;
  void disarm_page_flip(int handle, PageFlipCookie& cookie) noexcept override;
  [[nodiscard]] DrmEvent service_events(int handle, short revents) override;

  void queue_page_flip(std::uint64_t token, std::uint32_t crtc_id,
                       std::uint32_t sequence);
  void queue_error(std::string error);

  [[nodiscard]] bool open() const noexcept { return open_; }
  [[nodiscard]] std::size_t close_count() const noexcept { return close_count_; }
  [[nodiscard]] const DeviceOpenOptions& last_options() const noexcept {
    return last_options_;
  }

 private:
  void signal_event();

  static constexpr int kHandle = 1;
  FakeDeviceConfig config_;
  int event_pipe_[2]{-1, -1};
  bool open_{};
  std::size_t close_count_{};
  DeviceOpenOptions last_options_;
  PageFlipCookie* armed_cookie_{};
  std::deque<DrmEvent> events_;
};

}  // namespace glasswyrm::drm
