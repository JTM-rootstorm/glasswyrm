#pragma once

#include "backends/drm/resources.hpp"
#include "backends/drm/vrr_timing.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace glasswyrm::drm {

struct DriverMetadata {
  std::string name;
  std::string date;
  std::string description;
  std::string bus_info;
  int major{};
  int minor{};
  int patchlevel{};
};

struct DeviceSnapshot {
  std::string canonical_path;
  std::string sysfs_identity;
  std::uint32_t device_major{};
  std::uint32_t device_minor{};
  DriverMetadata driver;
  bool primary_node{};
  bool dumb_buffer{};
  bool universal_planes{};
  bool atomic{};
  std::vector<Connector> connectors;
  std::vector<Crtc> crtcs;
  std::vector<Plane> planes;
  bool timestamp_monotonic{};
};

struct DeviceOpenOptions {
  bool request_universal_planes{true};
  bool request_atomic{true};
};

enum class DeviceOpenStatus {
  Success,
  InvalidPath,
  OpenFailed,
  NotCharacterDevice,
  NotPrimaryNode,
  DriverQueryFailed,
  MissingDumbBuffer,
  MissingResources,
  ResourceQueryFailed,
};

struct DeviceOpenResult {
  DeviceOpenStatus status{DeviceOpenStatus::OpenFailed};
  int handle{-1};
  DeviceSnapshot snapshot;
  std::string error;
};

enum class DrmEventKind { None, PageFlip, Error };

struct DrmEvent {
  DrmEvent() = default;
  DrmEvent(DrmEventKind event_kind, std::uint64_t event_token,
           std::uint32_t event_crtc_id, std::uint32_t event_sequence,
           std::string event_error = {},
           std::uint64_t event_timestamp_nanoseconds = 0,
           bool event_timestamp_available = false,
           CrtcSequenceSample event_crtc_sequence_sample = {})
      : kind(event_kind), token(event_token), crtc_id(event_crtc_id),
        sequence(event_sequence), error(std::move(event_error)),
        kernel_timestamp_nanoseconds(event_timestamp_nanoseconds),
        timestamp_available(event_timestamp_available),
        crtc_sequence_sample(event_crtc_sequence_sample) {}

  DrmEventKind kind{DrmEventKind::None};
  std::uint64_t token{};
  std::uint32_t crtc_id{};
  std::uint32_t sequence{};
  std::string error;
  std::uint64_t kernel_timestamp_nanoseconds{};
  bool timestamp_available{};
  CrtcSequenceSample crtc_sequence_sample;
};

struct PageFlipCookie {
  explicit PageFlipCookie(const std::uint64_t value) : token(value) {}
  PageFlipCookie(const PageFlipCookie &) = delete;
  PageFlipCookie &operator=(const PageFlipCookie &) = delete;

  std::uint64_t token{};
  std::uint32_t completed_crtc_id{};
  std::uint32_t completed_sequence{};
  bool completed{};
  std::uint64_t kernel_timestamp_nanoseconds{};
  bool timestamp_available{};
  bool timestamp_invalid{};
  CrtcSequenceSample crtc_sequence_sample;
};

class DrmApi {
public:
  virtual ~DrmApi() = default;

  [[nodiscard]] virtual DeviceOpenResult
  open_device(std::string_view path, const DeviceOpenOptions &options) = 0;
  [[nodiscard]] virtual DeviceOpenResult
  adopt_device(int inherited_fd, const DeviceOpenOptions &options) = 0;
  virtual void close_device(int handle) noexcept = 0;
  [[nodiscard]] virtual int poll_fd(int handle) const noexcept = 0;
  [[nodiscard]] virtual int duplicate_fd(int handle, std::string &error) = 0;
  [[nodiscard]] virtual bool
  arm_page_flip(int handle, const std::shared_ptr<PageFlipCookie> &cookie,
                std::string &error) = 0;
  virtual void
  cancel_page_flip(int handle,
                   const std::shared_ptr<PageFlipCookie> &cookie) noexcept = 0;
  virtual void
  abandon_page_flip(int handle,
                    const std::shared_ptr<PageFlipCookie> &cookie) noexcept = 0;
  virtual void reset_crtc_sequence_samples(int handle) noexcept = 0;
  [[nodiscard]] virtual DrmEvent service_events(int handle, short revents) = 0;
};

[[nodiscard]] std::unique_ptr<DrmApi> make_real_drm_api();

} // namespace glasswyrm::drm
