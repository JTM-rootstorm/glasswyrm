#include "backends/drm/drm_api.hpp"

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <memory>
#include <optional>
#include <poll.h>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <unordered_map>
#include <utility>

namespace glasswyrm::drm {
namespace {

class UniqueFd {
public:
  explicit UniqueFd(const int value) noexcept : value_(value) {}
  ~UniqueFd() {
    if (value_ >= 0)
      (void)::close(value_);
  }
  UniqueFd(const UniqueFd &) = delete;
  UniqueFd &operator=(const UniqueFd &) = delete;
  [[nodiscard]] int release() noexcept { return std::exchange(value_, -1); }

private:
  int value_{-1};
};

struct VersionDeleter {
  void operator()(drmVersion *value) const noexcept { drmFreeVersion(value); }
};
struct ResourcesDeleter {
  void operator()(drmModeRes *value) const noexcept {
    drmModeFreeResources(value);
  }
};
struct CrtcDeleter {
  void operator()(drmModeCrtc *value) const noexcept { drmModeFreeCrtc(value); }
};
struct ConnectorDeleter {
  void operator()(drmModeConnector *value) const noexcept {
    drmModeFreeConnector(value);
  }
};
struct EncoderDeleter {
  void operator()(drmModeEncoder *value) const noexcept {
    drmModeFreeEncoder(value);
  }
};
struct PlaneResourcesDeleter {
  void operator()(drmModePlaneRes *value) const noexcept {
    drmModeFreePlaneResources(value);
  }
};
struct PlaneDeleter {
  void operator()(drmModePlane *value) const noexcept {
    drmModeFreePlane(value);
  }
};
struct ObjectPropertiesDeleter {
  void operator()(drmModeObjectProperties *value) const noexcept {
    drmModeFreeObjectProperties(value);
  }
};
struct PropertyDeleter {
  void operator()(drmModePropertyRes *value) const noexcept {
    drmModeFreeProperty(value);
  }
};
struct BusIdDeleter {
  void operator()(char *value) const noexcept { drmFreeBusid(value); }
};

std::string text(const char *value, const std::size_t length) {
  return value ? std::string(value, length) : std::string{};
}

ConnectionStatus connection_status(const drmModeConnection value) noexcept {
  switch (value) {
  case DRM_MODE_CONNECTED:
    return ConnectionStatus::Connected;
  case DRM_MODE_DISCONNECTED:
    return ConnectionStatus::Disconnected;
  case DRM_MODE_UNKNOWNCONNECTION:
    return ConnectionStatus::Unknown;
  }
  return ConnectionStatus::Unknown;
}

std::optional<std::uint64_t> property_value(const int fd,
                                            const std::uint32_t object_id,
                                            const std::uint32_t object_type,
                                            const std::string_view name) {
  const std::unique_ptr<drmModeObjectProperties, ObjectPropertiesDeleter>
      properties(drmModeObjectGetProperties(fd, object_id, object_type));
  if (!properties)
    return std::nullopt;
  for (std::uint32_t index = 0; index < properties->count_props; ++index) {
    const std::unique_ptr<drmModePropertyRes, PropertyDeleter> property(
        drmModeGetProperty(fd, properties->props[index]));
    if (property && name == property->name)
      return properties->prop_values[index];
  }
  return std::nullopt;
}

PlaneType plane_type(const std::optional<std::uint64_t> value) noexcept {
  if (!value)
    return PlaneType::Unknown;
  switch (*value) {
  case DRM_PLANE_TYPE_PRIMARY:
    return PlaneType::Primary;
  case DRM_PLANE_TYPE_CURSOR:
    return PlaneType::Cursor;
  case DRM_PLANE_TYPE_OVERLAY:
    return PlaneType::Overlay;
  default:
    return PlaneType::Unknown;
  }
}

std::uint32_t mode_refresh_millihz(const drmModeModeInfo &mode) noexcept {
  if (mode.htotal == 0 || mode.vtotal == 0)
    return 0;
  std::uint64_t refresh =
      static_cast<std::uint64_t>(mode.clock) * 1'000'000ULL /
      (static_cast<std::uint64_t>(mode.htotal) * mode.vtotal);
  if ((mode.flags & DRM_MODE_FLAG_INTERLACE) != 0)
    refresh *= 2U;
  if ((mode.flags & DRM_MODE_FLAG_DBLSCAN) != 0)
    refresh /= 2U;
  if (mode.vscan > 1)
    refresh /= mode.vscan;
  return static_cast<std::uint32_t>(refresh);
}

Mode discovered_mode(const drmModeModeInfo &mode) {
  Mode value;
  value.name = mode.name;
  value.width = mode.hdisplay;
  value.height = mode.vdisplay;
  value.refresh_millihz = mode_refresh_millihz(mode);
  value.clock_khz = mode.clock;
  value.preferred = (mode.type & DRM_MODE_TYPE_PREFERRED) != 0;
  value.hsync_start = mode.hsync_start;
  value.hsync_end = mode.hsync_end;
  value.htotal = mode.htotal;
  value.hskew = mode.hskew;
  value.vsync_start = mode.vsync_start;
  value.vsync_end = mode.vsync_end;
  value.vtotal = mode.vtotal;
  value.vscan = mode.vscan;
  value.flags = mode.flags;
  value.type = mode.type;
  value.vrefresh_hz = mode.vrefresh;
  return value;
}

DeviceOpenResult failure(const DeviceOpenStatus status, std::string error) {
  return {status, -1, {}, std::move(error)};
}

bool enumerate_connectors(const int fd, const drmModeRes &resources,
                          DeviceSnapshot &snapshot, std::string &error) {
  snapshot.crtcs.reserve(static_cast<std::size_t>(resources.count_crtcs));
  for (int index = 0; index < resources.count_crtcs; ++index) {
    const std::unique_ptr<drmModeCrtc, CrtcDeleter> native(
        drmModeGetCrtc(fd, resources.crtcs[index]));
    if (!native) {
      error = "cannot query DRM CRTC";
      return false;
    }
    Crtc discovered;
    discovered.id = native->crtc_id;
    discovered.index = static_cast<std::uint32_t>(index);
    discovered.framebuffer_id = native->buffer_id;
    discovered.x = native->x;
    discovered.y = native->y;
    discovered.active = native->mode_valid != 0;
    if (discovered.active)
      discovered.mode = discovered_mode(native->mode);
    snapshot.crtcs.push_back(std::move(discovered));
  }

  snapshot.connectors.reserve(
      static_cast<std::size_t>(resources.count_connectors));
  for (int index = 0; index < resources.count_connectors; ++index) {
    const std::unique_ptr<drmModeConnector, ConnectorDeleter> connector(
        drmModeGetConnector(fd, resources.connectors[index]));
    if (!connector) {
      error = "cannot query DRM connector";
      return false;
    }

    Connector discovered;
    discovered.id = connector->connector_id;
    discovered.type = connector->connector_type;
    discovered.type_id = connector->connector_type_id;
    discovered.status = connection_status(connector->connection);
    discovered.non_desktop =
        property_value(fd, connector->connector_id, DRM_MODE_OBJECT_CONNECTOR,
                       "non-desktop")
            .value_or(0) != 0;
    for (int mode_index = 0; mode_index < connector->count_modes;
         ++mode_index) {
      const auto &mode = connector->modes[mode_index];
      discovered.modes.push_back(discovered_mode(mode));
    }
    for (int encoder_index = 0; encoder_index < connector->count_encoders;
         ++encoder_index) {
      const std::unique_ptr<drmModeEncoder, EncoderDeleter> encoder(
          drmModeGetEncoder(fd, connector->encoders[encoder_index]));
      if (encoder)
        discovered.possible_crtc_mask |= encoder->possible_crtcs;
    }
    if (connector->encoder_id != 0) {
      const std::unique_ptr<drmModeEncoder, EncoderDeleter> encoder(
          drmModeGetEncoder(fd, connector->encoder_id));
      if (encoder)
        discovered.current_crtc_id = encoder->crtc_id;
    }
    if (discovered.current_crtc_id != 0) {
      for (auto &crtc : snapshot.crtcs)
        if (crtc.id == discovered.current_crtc_id)
          crtc.connector_ids.push_back(discovered.id);
    }
    snapshot.connectors.push_back(std::move(discovered));
  }
  return true;
}

bool enumerate_planes(const int fd, DeviceSnapshot &snapshot,
                      std::string &error) {
  if (!snapshot.universal_planes)
    return true;
  const std::unique_ptr<drmModePlaneRes, PlaneResourcesDeleter> planes(
      drmModeGetPlaneResources(fd));
  if (!planes) {
    error = "cannot enumerate DRM plane resources";
    return false;
  }
  snapshot.planes.reserve(planes->count_planes);
  for (std::uint32_t index = 0; index < planes->count_planes; ++index) {
    const std::unique_ptr<drmModePlane, PlaneDeleter> plane(
        drmModeGetPlane(fd, planes->planes[index]));
    if (!plane) {
      error = "cannot query DRM plane";
      return false;
    }
    Plane discovered;
    discovered.id = plane->plane_id;
    discovered.type = plane_type(
        property_value(fd, plane->plane_id, DRM_MODE_OBJECT_PLANE, "type"));
    discovered.possible_crtc_mask = plane->possible_crtcs;
    discovered.current_crtc_id = plane->crtc_id;
    discovered.framebuffer_id = plane->fb_id;
    const auto current = [&](const std::string_view name) {
      return property_value(fd, plane->plane_id, DRM_MODE_OBJECT_PLANE, name)
          .value_or(0);
    };
    discovered.crtc_x = static_cast<std::int32_t>(current("CRTC_X"));
    discovered.crtc_y = static_cast<std::int32_t>(current("CRTC_Y"));
    discovered.crtc_width = static_cast<std::uint32_t>(current("CRTC_W"));
    discovered.crtc_height = static_cast<std::uint32_t>(current("CRTC_H"));
    discovered.source_x = static_cast<std::uint32_t>(current("SRC_X"));
    discovered.source_y = static_cast<std::uint32_t>(current("SRC_Y"));
    discovered.source_width = static_cast<std::uint32_t>(current("SRC_W"));
    discovered.source_height = static_cast<std::uint32_t>(current("SRC_H"));
    discovered.formats.assign(plane->formats,
                              plane->formats + plane->count_formats);
    snapshot.planes.push_back(std::move(discovered));
  }
  return true;
}

std::string inherited_device_path(const int fd) {
  std::error_code error;
  const auto link = std::filesystem::read_symlink(
      "/proc/self/fd/" + std::to_string(fd), error);
  if (error)
    return "inherited-fd:" + std::to_string(fd);
  const auto canonical = std::filesystem::canonical(link, error);
  return error ? link.string() : canonical.string();
}

DeviceOpenResult inspect_device(const int fd, std::string path,
                                const DeviceOpenOptions &options) {
  struct stat status{};
  if (::fstat(fd, &status) != 0)
    return failure(DeviceOpenStatus::OpenFailed,
                   std::string("cannot inspect DRM device FD: ") +
                       std::strerror(errno));
  if (!S_ISCHR(status.st_mode))
    return failure(DeviceOpenStatus::NotCharacterDevice,
                   "DRM device is not a character device");
  if (drmGetNodeTypeFromFd(fd) != DRM_NODE_PRIMARY)
    return failure(DeviceOpenStatus::NotPrimaryNode,
                   "DRM device is not a primary node");

  const std::unique_ptr<drmVersion, VersionDeleter> version(drmGetVersion(fd));
  if (!version)
    return failure(DeviceOpenStatus::DriverQueryFailed,
                   "cannot query DRM driver metadata");

  DeviceSnapshot snapshot;
  snapshot.canonical_path = std::move(path);
  snapshot.device_major = static_cast<std::uint32_t>(::major(status.st_rdev));
  snapshot.device_minor = static_cast<std::uint32_t>(::minor(status.st_rdev));
  snapshot.primary_node = true;
  snapshot.driver.name = text(version->name, version->name_len);
  snapshot.driver.date = text(version->date, version->date_len);
  snapshot.driver.description = text(version->desc, version->desc_len);
  snapshot.driver.major = version->version_major;
  snapshot.driver.minor = version->version_minor;
  snapshot.driver.patchlevel = version->version_patchlevel;
  const std::unique_ptr<char, BusIdDeleter> bus(drmGetBusid(fd));
  if (bus)
    snapshot.driver.bus_info = bus.get();

  std::uint64_t dumb_buffer{};
  if (drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &dumb_buffer) != 0 || dumb_buffer == 0)
    return failure(DeviceOpenStatus::MissingDumbBuffer,
                   "DRM device does not support dumb buffers");
  snapshot.dumb_buffer = true;

  if (options.request_universal_planes)
    snapshot.universal_planes =
        drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) == 0;
  if (options.request_atomic && snapshot.universal_planes)
    snapshot.atomic = drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) == 0;

  const std::unique_ptr<drmModeRes, ResourcesDeleter> resources(
      drmModeGetResources(fd));
  if (!resources)
    return failure(DeviceOpenStatus::ResourceQueryFailed,
                   "cannot enumerate DRM mode resources");
  if (resources->count_crtcs <= 0 || resources->count_connectors <= 0)
    return failure(DeviceOpenStatus::MissingResources,
                   "DRM device has no CRTC or connector resources");

  std::string resource_error;
  if (!enumerate_connectors(fd, *resources, snapshot, resource_error) ||
      !enumerate_planes(fd, snapshot, resource_error))
    return failure(DeviceOpenStatus::ResourceQueryFailed,
                   std::move(resource_error));
  return {DeviceOpenStatus::Success, fd, std::move(snapshot), {}};
}

void page_flip_complete(int, const unsigned int sequence, unsigned int,
                        unsigned int, const unsigned int crtc_id,
                        void *user_data) {
  if (!user_data)
    return;
  auto &cookie = *static_cast<PageFlipCookie *>(user_data);
  cookie.completed_crtc_id = crtc_id;
  cookie.completed_sequence = sequence;
  cookie.completed = true;
}

class RealDrmApi final : public DrmApi {
public:
  DeviceOpenResult open_device(std::string_view path,
                               const DeviceOpenOptions &options) override;
  DeviceOpenResult adopt_device(int inherited_fd,
                                const DeviceOpenOptions &options) override;
  void close_device(int handle) noexcept override;
  int poll_fd(int handle) const noexcept override;
  int duplicate_fd(int handle, std::string &error) override;
  bool arm_page_flip(int handle, PageFlipCookie &cookie,
                     std::string &error) override;
  void disarm_page_flip(int handle, PageFlipCookie &cookie) noexcept override;
  DrmEvent service_events(int handle, short revents) override;

private:
  std::unordered_map<int, PageFlipCookie *> armed_cookies_;
};

DeviceOpenResult RealDrmApi::open_device(const std::string_view path,
                                         const DeviceOpenOptions &options) {
  std::error_code filesystem_error;
  const auto canonical = std::filesystem::canonical(
      std::filesystem::path(std::string(path)), filesystem_error);
  if (filesystem_error)
    return failure(DeviceOpenStatus::InvalidPath,
                   "cannot resolve DRM device path: " +
                       filesystem_error.message());

  const int raw_fd = ::open(canonical.c_str(), O_RDWR | O_CLOEXEC);
  if (raw_fd < 0)
    return failure(DeviceOpenStatus::OpenFailed,
                   std::string("cannot open DRM device: ") +
                       std::strerror(errno));
  UniqueFd fd(raw_fd);
  auto result = inspect_device(raw_fd, canonical.string(), options);
  if (result.status == DeviceOpenStatus::Success)
    result.handle = fd.release();
  return result;
}

DeviceOpenResult RealDrmApi::adopt_device(const int inherited_fd,
                                          const DeviceOpenOptions &options) {
  const int duplicate = ::fcntl(inherited_fd, F_DUPFD_CLOEXEC, 0);
  if (duplicate < 0)
    return failure(DeviceOpenStatus::OpenFailed,
                   std::string("cannot duplicate inherited DRM device FD: ") +
                       std::strerror(errno));
  UniqueFd fd(duplicate);
  auto result =
      inspect_device(duplicate, inherited_device_path(inherited_fd), options);
  if (result.status == DeviceOpenStatus::Success)
    result.handle = fd.release();
  return result;
}

void RealDrmApi::close_device(const int handle) noexcept {
  armed_cookies_.erase(handle);
  if (handle >= 0)
    (void)::close(handle);
}

int RealDrmApi::poll_fd(const int handle) const noexcept { return handle; }

int RealDrmApi::duplicate_fd(const int handle, std::string &error) {
  const int duplicate = ::fcntl(handle, F_DUPFD_CLOEXEC, 0);
  if (duplicate < 0) {
    error = std::string("DRM FD duplication failed: ") + std::strerror(errno);
    return -1;
  }
  error.clear();
  return duplicate;
}

bool RealDrmApi::arm_page_flip(const int handle, PageFlipCookie &cookie,
                               std::string &error) {
  if (handle < 0) {
    error = "DRM device is not open";
    return false;
  }
  if (armed_cookies_.contains(handle)) {
    error = "DRM page flip is already armed";
    return false;
  }
  cookie.completed = false;
  cookie.completed_crtc_id = 0;
  cookie.completed_sequence = 0;
  armed_cookies_.emplace(handle, &cookie);
  error.clear();
  return true;
}

void RealDrmApi::disarm_page_flip(const int handle,
                                  PageFlipCookie &cookie) noexcept {
  const auto armed = armed_cookies_.find(handle);
  if (armed != armed_cookies_.end() && armed->second == &cookie)
    armed_cookies_.erase(armed);
}

DrmEvent RealDrmApi::service_events(const int handle, const short revents) {
  if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
    return {DrmEventKind::Error, 0, 0, 0, "DRM event FD reported an error"};
  if ((revents & POLLIN) == 0)
    return {};
  const auto armed = armed_cookies_.find(handle);
  PageFlipCookie *const cookie =
      armed == armed_cookies_.end() ? nullptr : armed->second;
  drmEventContext context{};
  context.version = DRM_EVENT_CONTEXT_VERSION;
  context.page_flip_handler2 = page_flip_complete;
  if (drmHandleEvent(handle, &context) != 0)
    return {DrmEventKind::Error, 0, 0, 0,
            std::string("DRM event handling failed: ") + std::strerror(errno)};
  if (cookie && cookie->completed) {
    const DrmEvent event{DrmEventKind::PageFlip,
                         cookie->token,
                         cookie->completed_crtc_id,
                         cookie->completed_sequence,
                         {}};
    armed_cookies_.erase(handle);
    return event;
  }
  return {};
}

} // namespace

std::unique_ptr<DrmApi> make_real_drm_api() {
  return std::make_unique<RealDrmApi>();
}

} // namespace glasswyrm::drm
