#include "backends/drm/kms_state.hpp"

#include <utility>

namespace glasswyrm::drm {
namespace {
bool modes_equal(const KmsMode &left, const KmsMode &right) {
  return left.clock == right.clock && left.hdisplay == right.hdisplay &&
         left.hsync_start == right.hsync_start &&
         left.hsync_end == right.hsync_end && left.htotal == right.htotal &&
         left.hskew == right.hskew && left.vdisplay == right.vdisplay &&
         left.vsync_start == right.vsync_start &&
         left.vsync_end == right.vsync_end && left.vtotal == right.vtotal &&
         left.vscan == right.vscan && left.vrefresh == right.vrefresh &&
         left.flags == right.flags && left.type == right.type &&
         left.name == right.name;
}
} // namespace

ModeBlob::~ModeBlob() { reset(); }
ModeBlob::ModeBlob(ModeBlob &&other) noexcept { *this = std::move(other); }
ModeBlob &ModeBlob::operator=(ModeBlob &&other) noexcept {
  if (this == &other)
    return *this;
  reset();
  api_ = std::exchange(other.api_, nullptr);
  fd_ = std::exchange(other.fd_, -1);
  id_ = std::exchange(other.id_, 0);
  return *this;
}
bool ModeBlob::create(KmsApi &api, int fd, const KmsMode &mode,
                      std::string &error) {
  ModeBlob replacement;
  replacement.api_ = &api;
  replacement.fd_ = fd;
  if (!api.create_mode_blob(fd, mode, replacement.id_, error))
    return false;
  if (replacement.id_ == 0) {
    error = "DRM mode blob ID is zero";
    return false;
  }
  *this = std::move(replacement);
  return true;
}
void ModeBlob::reset() noexcept {
  if (api_ && id_)
    api_->destroy_mode_blob(fd_, id_);
  api_ = nullptr;
  fd_ = -1;
  id_ = 0;
}
void ModeBlob::abandon() noexcept {
  api_ = nullptr;
  fd_ = -1;
  id_ = 0;
}

bool capture_saved_state(KmsApi &api, int fd, PipelineIds ids,
                         std::span<const std::uint32_t> connectors, bool atomic,
                         SavedKmsState &output, std::string &error) {
  if (connectors.size() > 1) {
    error = "cloned CRTC state is unsupported";
    return false;
  }
  SavedKmsState value;
  value.pipeline = ids;
  value.atomic = atomic;
  value.connector_ids.assign(connectors.begin(), connectors.end());
  if (!api.read_connector_crtc(fd, ids.connector, value.connector_crtc_id,
                               error) ||
      !api.read_crtc(fd, ids.crtc, value.crtc, error))
    return false;
  if (atomic) {
    if (!api.read_plane(fd, ids.primary_plane, value.primary_plane, error))
      return false;
    std::vector<ObjectProperty> connector_props, crtc_props, plane_props;
    if (!api.object_properties(fd, KmsObjectType::Connector, ids.connector,
                               connector_props, error) ||
        !api.object_properties(fd, KmsObjectType::Crtc, ids.crtc, crtc_props,
                               error) ||
        !api.object_properties(fd, KmsObjectType::Plane, ids.primary_plane,
                               plane_props, error))
      return false;
    const auto cache =
        build_atomic_property_cache(connector_props, crtc_props, plane_props);
    if (cache.status != PropertyCacheStatus::Success) {
      error = "required atomic property is missing: " + cache.property_name;
      return false;
    }
    value.properties = cache.cache;
  }
  output = std::move(value);
  error.clear();
  return true;
}

std::vector<AtomicPropertyValue>
atomic_initial_request(PipelineIds ids, const AtomicPropertyCache &p,
                       std::uint32_t blob, std::uint32_t fb,
                       std::uint32_t width, std::uint32_t height) {
  std::vector<AtomicPropertyValue> request{
      {ids.connector, p.connector.crtc_id.id, ids.crtc},
          {ids.crtc, p.crtc.mode_id.id, blob},
          {ids.crtc, p.crtc.active.id, 1},
          {ids.primary_plane, p.primary_plane.fb_id.id, fb},
          {ids.primary_plane, p.primary_plane.crtc_id.id, ids.crtc},
          {ids.primary_plane, p.primary_plane.src_x.id, 0},
          {ids.primary_plane, p.primary_plane.src_y.id, 0},
          {ids.primary_plane, p.primary_plane.src_w.id,
           std::uint64_t{width} << 16U},
          {ids.primary_plane, p.primary_plane.src_h.id,
           std::uint64_t{height} << 16U},
          {ids.primary_plane, p.primary_plane.crtc_x.id, 0},
          {ids.primary_plane, p.primary_plane.crtc_y.id, 0},
          {ids.primary_plane, p.primary_plane.crtc_w.id, width},
      {ids.primary_plane, p.primary_plane.crtc_h.id, height}};
  if (p.crtc.vrr_enabled)
    request.push_back({ids.crtc, p.crtc.vrr_enabled->id, 0});
  return request;
}
std::vector<AtomicPropertyValue>
atomic_flip_request(PipelineIds ids, const AtomicPropertyCache &p,
                    std::uint32_t fb) {
  return {{ids.primary_plane, p.primary_plane.fb_id.id, fb}};
}

bool verify_saved_state(KmsApi &api, int fd, const SavedKmsState &saved,
                        std::string &error) {
  std::uint32_t connector_crtc{};
  KmsCrtcState crtc;
  if (!api.read_connector_crtc(fd, saved.pipeline.connector, connector_crtc,
                               error) ||
      !api.read_crtc(fd, saved.pipeline.crtc, crtc, error))
    return false;
  if (connector_crtc != saved.connector_crtc_id ||
      crtc.framebuffer_id != saved.crtc.framebuffer_id ||
      crtc.x != saved.crtc.x || crtc.y != saved.crtc.y ||
      crtc.active != saved.crtc.active ||
      (saved.crtc.active && !modes_equal(crtc.mode, saved.crtc.mode))) {
    error = "restored DRM CRTC state does not match the saved state";
    return false;
  }
  if (saved.atomic) {
    KmsPlaneState plane;
    if (!api.read_plane(fd, saved.pipeline.primary_plane, plane, error))
      return false;
    if (plane.framebuffer_id != saved.primary_plane.framebuffer_id ||
        plane.crtc_id != saved.primary_plane.crtc_id ||
        plane.src_x != saved.primary_plane.src_x ||
        plane.src_y != saved.primary_plane.src_y ||
        plane.src_w != saved.primary_plane.src_w ||
        plane.src_h != saved.primary_plane.src_h ||
        plane.crtc_x != saved.primary_plane.crtc_x ||
        plane.crtc_y != saved.primary_plane.crtc_y ||
        plane.crtc_w != saved.primary_plane.crtc_w ||
        plane.crtc_h != saved.primary_plane.crtc_h) {
      error = "restored DRM primary-plane state does not match the saved state";
      return false;
    }
    if (saved.properties.crtc.vrr_enabled) {
      std::vector<ObjectProperty> properties;
      if (!api.object_properties(fd, KmsObjectType::Crtc,
                                 saved.pipeline.crtc, properties, error))
        return false;
      const ObjectProperty* restored = nullptr;
      for (const auto &property : properties) {
        if (property.name != "VRR_ENABLED")
          continue;
        if (restored != nullptr) {
          error = "restored DRM VRR_ENABLED property is duplicated";
          return false;
        }
        restored = &property;
      }
      const auto &expected = *saved.properties.crtc.vrr_enabled;
      if (restored == nullptr || restored->id != expected.id ||
          restored->value != expected.value) {
        error = "restored DRM VRR_ENABLED state does not match the saved state";
        return false;
      }
    }
  }
  error.clear();
  return true;
}

bool restore_saved_state(KmsApi &api, int fd, const SavedKmsState &saved,
                         std::string &error) {
  if (!saved.atomic) {
    if (!api.legacy_set_crtc(fd, saved.crtc, saved.connector_ids, error))
      return false;
    return verify_saved_state(api, fd, saved, error);
  }
  ModeBlob blob;
  if (saved.crtc.active && !blob.create(api, fd, saved.crtc.mode, error))
    return false;
  const auto &p = saved.properties;
  const auto ids = saved.pipeline;
  std::vector<AtomicPropertyValue> values{
      {ids.connector, p.connector.crtc_id.id, saved.connector_crtc_id},
      {ids.crtc, p.crtc.mode_id.id, blob.id()},
      {ids.crtc, p.crtc.active.id, saved.crtc.active ? 1U : 0U},
      {ids.primary_plane, p.primary_plane.fb_id.id,
       saved.primary_plane.framebuffer_id},
      {ids.primary_plane, p.primary_plane.crtc_id.id,
       saved.primary_plane.crtc_id},
      {ids.primary_plane, p.primary_plane.src_x.id, saved.primary_plane.src_x},
      {ids.primary_plane, p.primary_plane.src_y.id, saved.primary_plane.src_y},
      {ids.primary_plane, p.primary_plane.src_w.id, saved.primary_plane.src_w},
      {ids.primary_plane, p.primary_plane.src_h.id, saved.primary_plane.src_h},
      {ids.primary_plane, p.primary_plane.crtc_x.id,
       static_cast<std::uint64_t>(
           static_cast<std::int64_t>(saved.primary_plane.crtc_x))},
      {ids.primary_plane, p.primary_plane.crtc_y.id,
       static_cast<std::uint64_t>(
           static_cast<std::int64_t>(saved.primary_plane.crtc_y))},
      {ids.primary_plane, p.primary_plane.crtc_w.id,
       saved.primary_plane.crtc_w},
      {ids.primary_plane, p.primary_plane.crtc_h.id,
       saved.primary_plane.crtc_h}};
  if (p.crtc.vrr_enabled)
    values.push_back(
        {ids.crtc, p.crtc.vrr_enabled->id, p.crtc.vrr_enabled->value});
  if (!api.atomic_commit(fd, values, AtomicAllowModeset, nullptr, error))
    return false;
  return verify_saved_state(api, fd, saved, error);
}
} // namespace glasswyrm::drm
