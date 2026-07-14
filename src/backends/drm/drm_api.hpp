#pragma once

#include "backends/drm/resources.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

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
  DrmEventKind kind{DrmEventKind::None};
  std::uint64_t token{};
  std::uint32_t crtc_id{};
  std::uint32_t sequence{};
  std::string error;
};

struct PageFlipCookie {
  explicit PageFlipCookie(const std::uint64_t value) : token(value) {}
  PageFlipCookie(const PageFlipCookie &) = delete;
  PageFlipCookie &operator=(const PageFlipCookie &) = delete;

  std::uint64_t token{};
  std::uint32_t completed_crtc_id{};
  std::uint32_t completed_sequence{};
  bool completed{};
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
  [[nodiscard]] virtual bool arm_page_flip(int handle, PageFlipCookie &cookie,
                                           std::string &error) = 0;
  virtual void disarm_page_flip(int handle,
                                PageFlipCookie &cookie) noexcept = 0;
  [[nodiscard]] virtual DrmEvent service_events(int handle, short revents) = 0;
};

[[nodiscard]] std::unique_ptr<DrmApi> make_real_drm_api();

} // namespace glasswyrm::drm
