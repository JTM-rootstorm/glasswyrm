#include "backends/drm/kms_api.hpp"
#include <cerrno>
#include <cstring>
#include <memory>
#include <sys/mman.h>
#include <utility>
#include <xf86drm.h>
#include <xf86drmMode.h>
namespace glasswyrm::drm {
namespace {
template <class T, void (*Free)(T *)> struct Deleter {
  void operator()(T *p) const noexcept {
    if (p)
      Free(p);
  }
};
using Props = std::unique_ptr<
    drmModeObjectProperties,
    Deleter<drmModeObjectProperties, drmModeFreeObjectProperties>>;
using Prop = std::unique_ptr<drmModePropertyRes,
                             Deleter<drmModePropertyRes, drmModeFreeProperty>>;
using CrtcPtr =
    std::unique_ptr<drmModeCrtc, Deleter<drmModeCrtc, drmModeFreeCrtc>>;
using ConnPtr =
    std::unique_ptr<drmModeConnector,
                    Deleter<drmModeConnector, drmModeFreeConnector>>;
using EncPtr = std::unique_ptr<drmModeEncoder,
                               Deleter<drmModeEncoder, drmModeFreeEncoder>>;
using PlanePtr =
    std::unique_ptr<drmModePlane, Deleter<drmModePlane, drmModeFreePlane>>;
using AtomicPtr = std::unique_ptr<drmModeAtomicReq,
                                  Deleter<drmModeAtomicReq, drmModeAtomicFree>>;
bool fail(std::string &e, const char *what) {
  e = std::string(what) + ": " + std::strerror(errno);
  return false;
}
bool fail_code(std::string &e, const char *what, const int code) {
  e = std::string(what) + ": " + std::strerror(code);
  return false;
}
std::uint32_t object_type(KmsObjectType t) {
  switch (t) {
  case KmsObjectType::Connector:
    return DRM_MODE_OBJECT_CONNECTOR;
  case KmsObjectType::Crtc:
    return DRM_MODE_OBJECT_CRTC;
  case KmsObjectType::Plane:
    return DRM_MODE_OBJECT_PLANE;
  }
  return 0;
}
KmsMode from_mode(const drmModeModeInfo &m) {
  return {m.clock, m.hdisplay, m.hsync_start, m.hsync_end, m.htotal,
          m.hskew, m.vdisplay, m.vsync_start, m.vsync_end, m.vtotal,
          m.vscan, m.vrefresh, m.flags,       m.type,      m.name};
}
drmModeModeInfo to_mode(const KmsMode &m) {
  drmModeModeInfo v{};
  v.clock = m.clock;
  v.hdisplay = m.hdisplay;
  v.hsync_start = m.hsync_start;
  v.hsync_end = m.hsync_end;
  v.htotal = m.htotal;
  v.hskew = m.hskew;
  v.vdisplay = m.vdisplay;
  v.vsync_start = m.vsync_start;
  v.vsync_end = m.vsync_end;
  v.vtotal = m.vtotal;
  v.vscan = m.vscan;
  v.vrefresh = m.vrefresh;
  v.flags = m.flags;
  v.type = m.type;
  std::strncpy(v.name, m.name.c_str(), sizeof(v.name) - 1);
  return v;
}
class RealKmsApi final : public KmsApi {
public:
  bool is_master(int, bool &, std::string &) override;
  bool acquire_master(int, std::string &) override;
  bool drop_master(int, std::string &) override;
  bool create_dumb(int, std::uint32_t, std::uint32_t, std::uint32_t,
                   DumbAllocation &, std::string &) override;
  bool add_framebuffer2(int, std::uint32_t, std::uint32_t, std::uint32_t,
                        std::uint32_t, std::uint32_t, std::uint32_t &,
                        std::string &) override;
  bool map_dumb(int, std::uint32_t, std::uint64_t &, std::string &) override;
  std::byte *map_memory(int, std::uint64_t, std::size_t,
                        std::string &) override;
  bool remove_framebuffer(int, std::uint32_t, std::string &) noexcept override;
  bool unmap_memory(std::byte *, std::size_t, std::string &) noexcept override;
  bool destroy_dumb(int, std::uint32_t, std::string &) noexcept override;
  bool object_properties(int, KmsObjectType, std::uint32_t,
                         std::vector<ObjectProperty> &, std::string &) override;
  bool read_connector_crtc(int, std::uint32_t, std::uint32_t &,
                           std::string &) override;
  bool read_crtc(int, std::uint32_t, KmsCrtcState &, std::string &) override;
  bool read_plane(int, std::uint32_t, KmsPlaneState &, std::string &) override;
  bool create_mode_blob(int, const KmsMode &, std::uint32_t &,
                        std::string &) override;
  void destroy_mode_blob(int, std::uint32_t) noexcept override;
  bool atomic_commit(int, std::span<const AtomicPropertyValue>, std::uint32_t,
                     PageFlipCookie *, std::string &) override;
  bool legacy_set_crtc(int, const KmsCrtcState &,
                       std::span<const std::uint32_t>, std::string &) override;
  bool legacy_page_flip(int, std::uint32_t, std::uint32_t, PageFlipCookie &,
                        std::string &) override;
};
bool RealKmsApi::is_master(int fd, bool &master, std::string &error) {
  errno = 0;
  const int result = drmIsMaster(fd);
  if (result < 0)
    return fail(error, "drmIsMaster failed");
  master = result != 0;
  error.clear();
  return true;
}
bool RealKmsApi::acquire_master(int fd, std::string &e) {
  if (drmSetMaster(fd) != 0)
    return fail(e, "drmSetMaster failed");
  e.clear();
  return true;
}
bool RealKmsApi::drop_master(int fd, std::string &e) {
  if (drmDropMaster(fd) != 0)
    return fail(e, "drmDropMaster failed");
  e.clear();
  return true;
}
bool RealKmsApi::create_dumb(int fd, std::uint32_t w, std::uint32_t h,
                             std::uint32_t b, DumbAllocation &out,
                             std::string &e) {
  drm_mode_create_dumb c{};
  c.width = w;
  c.height = h;
  c.bpp = b;
  if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &c) != 0)
    return fail(e, "CREATE_DUMB failed");
  out = {c.handle, c.pitch, c.size};
  e.clear();
  return true;
}
bool RealKmsApi::add_framebuffer2(int fd, std::uint32_t handle, std::uint32_t w,
                                  std::uint32_t h, std::uint32_t pitch,
                                  std::uint32_t format, std::uint32_t &fb,
                                  std::string &e) {
  std::uint32_t handles[4]{handle}, pitches[4]{pitch}, offsets[4]{};
  if (drmModeAddFB2(fd, w, h, format, handles, pitches, offsets, &fb, 0) != 0)
    return fail(e, "drmModeAddFB2 failed");
  e.clear();
  return true;
}
bool RealKmsApi::map_dumb(int fd, std::uint32_t handle, std::uint64_t &off,
                          std::string &e) {
  drm_mode_map_dumb m{};
  m.handle = handle;
  if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m) != 0)
    return fail(e, "MAP_DUMB failed");
  off = m.offset;
  e.clear();
  return true;
}
std::byte *RealKmsApi::map_memory(int fd, std::uint64_t off, std::size_t size,
                                  std::string &e) {
  void *p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                 static_cast<off_t>(off));
  if (p == MAP_FAILED) {
    fail(e, "dumb-buffer mmap failed");
    return nullptr;
  }
  e.clear();
  return static_cast<std::byte *>(p);
}
bool RealKmsApi::remove_framebuffer(int fd, std::uint32_t fb,
                                    std::string &error) noexcept {
  if (drmModeRmFB(fd, fb) != 0)
    return fail(error, "drmModeRmFB failed");
  error.clear();
  return true;
}
bool RealKmsApi::unmap_memory(std::byte *p, std::size_t s,
                              std::string &error) noexcept {
  if (munmap(p, s) != 0)
    return fail(error, "dumb-buffer munmap failed");
  error.clear();
  return true;
}
bool RealKmsApi::destroy_dumb(int fd, std::uint32_t h,
                              std::string &error) noexcept {
  drm_mode_destroy_dumb d{};
  d.handle = h;
  if (drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d) != 0)
    return fail(error, "DESTROY_DUMB failed");
  error.clear();
  return true;
}
bool RealKmsApi::object_properties(int fd, KmsObjectType type, std::uint32_t id,
                                   std::vector<ObjectProperty> &out,
                                   std::string &e) {
  Props props(drmModeObjectGetProperties(fd, id, object_type(type)));
  if (!props)
    return fail(e, "property enumeration failed");
  std::vector<ObjectProperty> result;
  result.reserve(props->count_props);
  for (std::uint32_t i = 0; i < props->count_props; ++i) {
    Prop p(drmModeGetProperty(fd, props->props[i]));
    if (!p)
      return fail(e, "property query failed");
    result.push_back({p->prop_id, p->name, props->prop_values[i], 64});
  }
  out = std::move(result);
  e.clear();
  return true;
}
bool RealKmsApi::read_connector_crtc(int fd, std::uint32_t id,
                                     std::uint32_t &crtc, std::string &e) {
  ConnPtr c(drmModeGetConnector(fd, id));
  if (!c)
    return fail(e, "connector query failed");
  crtc = 0;
  if (c->encoder_id) {
    EncPtr enc(drmModeGetEncoder(fd, c->encoder_id));
    if (!enc)
      return fail(e, "encoder query failed");
    crtc = enc->crtc_id;
  }
  e.clear();
  return true;
}
bool RealKmsApi::read_crtc(int fd, std::uint32_t id, KmsCrtcState &out,
                           std::string &e) {
  CrtcPtr c(drmModeGetCrtc(fd, id));
  if (!c)
    return fail(e, "CRTC query failed");
  out = {c->crtc_id,
         c->buffer_id,
         c->x,
         c->y,
         c->mode_valid != 0,
         c->mode_valid ? from_mode(c->mode) : KmsMode{}};
  e.clear();
  return true;
}
bool RealKmsApi::read_plane(int fd, std::uint32_t id, KmsPlaneState &out,
                            std::string &e) {
  PlanePtr p(drmModeGetPlane(fd, id));
  if (!p)
    return fail(e, "plane query failed");
  std::vector<ObjectProperty> props;
  if (!object_properties(fd, KmsObjectType::Plane, id, props, e))
    return false;
  auto value = [&](const char *name) {
    for (const auto &prop : props)
      if (prop.name == name)
        return static_cast<std::uint32_t>(prop.value);
    return 0U;
  };
  out = {p->plane_id,
         p->fb_id,
         p->crtc_id,
         static_cast<std::int32_t>(value("CRTC_X")),
         static_cast<std::int32_t>(value("CRTC_Y")),
         value("CRTC_W"),
         value("CRTC_H"),
         value("SRC_X"),
         value("SRC_Y"),
         value("SRC_W"),
         value("SRC_H")};
  e.clear();
  return true;
}
bool RealKmsApi::create_mode_blob(int fd, const KmsMode &mode,
                                  std::uint32_t &id, std::string &e) {
  auto native = to_mode(mode);
  if (drmModeCreatePropertyBlob(fd, &native, sizeof(native), &id) != 0)
    return fail(e, "mode blob creation failed");
  e.clear();
  return true;
}
void RealKmsApi::destroy_mode_blob(int fd, std::uint32_t id) noexcept {
  (void)drmModeDestroyPropertyBlob(fd, id);
}
bool RealKmsApi::atomic_commit(int fd,
                               std::span<const AtomicPropertyValue> values,
                               std::uint32_t flags, PageFlipCookie *cookie,
                               std::string &e) {
  AtomicPtr req(drmModeAtomicAlloc());
  if (!req) {
    e = "atomic request allocation failed";
    return false;
  }
  for (auto v : values) {
    const int added = drmModeAtomicAddProperty(req.get(), v.object_id,
                                               v.property_id, v.value);
    if (added < 0)
      return fail_code(e, "atomic property add failed", -added);
  }
  std::uint32_t native = 0;
  if (flags & AtomicTestOnly)
    native |= DRM_MODE_ATOMIC_TEST_ONLY;
  if (flags & AtomicAllowModeset)
    native |= DRM_MODE_ATOMIC_ALLOW_MODESET;
  if (flags & AtomicNonblock)
    native |= DRM_MODE_ATOMIC_NONBLOCK;
  if (flags & AtomicPageFlipEvent)
    native |= DRM_MODE_PAGE_FLIP_EVENT;
  if (drmModeAtomicCommit(fd, req.get(), native, cookie) != 0)
    return fail(e, "atomic commit failed");
  e.clear();
  return true;
}
bool RealKmsApi::legacy_set_crtc(int fd, const KmsCrtcState &s,
                                 std::span<const std::uint32_t> connectors,
                                 std::string &e) {
  auto mode = to_mode(s.mode);
  auto *ptr = s.active ? &mode : nullptr;
  std::vector<std::uint32_t> mutable_connectors(connectors.begin(),
                                                connectors.end());
  if (drmModeSetCrtc(fd, s.crtc_id, s.framebuffer_id, s.x, s.y,
                     mutable_connectors.data(), mutable_connectors.size(),
                     ptr) != 0)
    return fail(e, "legacy SetCrtc failed");
  e.clear();
  return true;
}
bool RealKmsApi::legacy_page_flip(int fd, std::uint32_t crtc, std::uint32_t fb,
                                  PageFlipCookie &cookie, std::string &e) {
  if (drmModePageFlip(fd, crtc, fb, DRM_MODE_PAGE_FLIP_EVENT, &cookie) != 0)
    return fail(e, "legacy PageFlip failed");
  e.clear();
  return true;
}
} // namespace
std::unique_ptr<KmsApi> make_real_kms_api() {
  return std::make_unique<RealKmsApi>();
}
} // namespace glasswyrm::drm
