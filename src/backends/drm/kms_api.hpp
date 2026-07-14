#pragma once

#include "backends/drm/drm_api.hpp"
#include "backends/drm/dumb_buffer_api.hpp"
#include "backends/drm/property_cache.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace glasswyrm::drm {

struct KmsMode {
  std::uint32_t clock{};
  std::uint16_t hdisplay{}, hsync_start{}, hsync_end{}, htotal{}, hskew{};
  std::uint16_t vdisplay{}, vsync_start{}, vsync_end{}, vtotal{}, vscan{};
  std::uint32_t vrefresh{}, flags{}, type{};
  std::string name;
};

[[nodiscard]] inline KmsMode kms_mode_from_discovered(const Mode &mode) {
  KmsMode kms;
  kms.clock = mode.clock_khz;
  kms.hdisplay = static_cast<std::uint16_t>(mode.width);
  kms.hsync_start = mode.hsync_start;
  kms.hsync_end = mode.hsync_end;
  kms.htotal = mode.htotal;
  kms.hskew = mode.hskew;
  kms.vdisplay = static_cast<std::uint16_t>(mode.height);
  kms.vsync_start = mode.vsync_start;
  kms.vsync_end = mode.vsync_end;
  kms.vtotal = mode.vtotal;
  kms.vscan = mode.vscan;
  kms.vrefresh = mode.vrefresh_hz;
  kms.flags = mode.flags;
  kms.type = mode.type;
  kms.name = mode.name;
  return kms;
}

struct KmsCrtcState {
  std::uint32_t crtc_id{}, framebuffer_id{}, x{}, y{};
  bool active{};
  KmsMode mode;
};

struct KmsPlaneState {
  std::uint32_t plane_id{}, framebuffer_id{}, crtc_id{};
  std::int32_t crtc_x{}, crtc_y{};
  std::uint32_t crtc_w{}, crtc_h{};
  std::uint32_t src_x{}, src_y{}, src_w{}, src_h{};
};

enum class KmsObjectType { Connector, Crtc, Plane };

struct AtomicPropertyValue {
  std::uint32_t object_id{};
  std::uint32_t property_id{};
  std::uint64_t value{};
};

enum AtomicCommitFlag : std::uint32_t {
  AtomicTestOnly = 1U << 0U,
  AtomicAllowModeset = 1U << 1U,
  AtomicNonblock = 1U << 2U,
  AtomicPageFlipEvent = 1U << 3U,
};

class KmsApi {
public:
  virtual ~KmsApi() = default;
  // Every fd is borrowed from Device; KmsApi never duplicates or closes it.
  [[nodiscard]] virtual bool is_master(int fd, bool &master,
                                       std::string &error) = 0;
  [[nodiscard]] virtual bool acquire_master(int fd, std::string &error) = 0;
  [[nodiscard]] virtual bool drop_master(int fd, std::string &error) = 0;

  [[nodiscard]] virtual bool create_dumb(int fd, std::uint32_t width,
                                         std::uint32_t height,
                                         std::uint32_t bits_per_pixel,
                                         DumbAllocation &allocation,
                                         std::string &error) = 0;
  [[nodiscard]] virtual bool
  add_framebuffer2(int fd, std::uint32_t handle, std::uint32_t width,
                   std::uint32_t height, std::uint32_t pitch,
                   std::uint32_t format, std::uint32_t &framebuffer_id,
                   std::string &error) = 0;
  [[nodiscard]] virtual bool map_dumb(int fd, std::uint32_t handle,
                                      std::uint64_t &offset,
                                      std::string &error) = 0;
  [[nodiscard]] virtual std::byte *map_memory(int fd, std::uint64_t offset,
                                              std::size_t size,
                                              std::string &error) = 0;
  [[nodiscard]] virtual bool
  remove_framebuffer(int fd, std::uint32_t framebuffer_id,
                     std::string &error) noexcept = 0;
  [[nodiscard]] virtual bool unmap_memory(std::byte *mapping, std::size_t size,
                                          std::string &error) noexcept = 0;
  [[nodiscard]] virtual bool destroy_dumb(int fd, std::uint32_t handle,
                                          std::string &error) noexcept = 0;

  [[nodiscard]] virtual bool
  object_properties(int fd, KmsObjectType type, std::uint32_t object_id,
                    std::vector<ObjectProperty> &properties,
                    std::string &error) = 0;
  [[nodiscard]] virtual bool read_connector_crtc(int fd,
                                                 std::uint32_t connector_id,
                                                 std::uint32_t &crtc_id,
                                                 std::string &error) = 0;
  [[nodiscard]] virtual bool read_crtc(int fd, std::uint32_t crtc_id,
                                       KmsCrtcState &state,
                                       std::string &error) = 0;
  [[nodiscard]] virtual bool read_plane(int fd, std::uint32_t plane_id,
                                        KmsPlaneState &state,
                                        std::string &error) = 0;
  [[nodiscard]] virtual bool create_mode_blob(int fd, const KmsMode &mode,
                                              std::uint32_t &blob_id,
                                              std::string &error) = 0;
  virtual void destroy_mode_blob(int fd, std::uint32_t blob_id) noexcept = 0;
  [[nodiscard]] virtual bool
  atomic_commit(int fd, std::span<const AtomicPropertyValue> properties,
                std::uint32_t flags, PageFlipCookie *cookie,
                std::string &error) = 0;
  [[nodiscard]] virtual bool
  legacy_set_crtc(int fd, const KmsCrtcState &state,
                  std::span<const std::uint32_t> connector_ids,
                  std::string &error) = 0;
  [[nodiscard]] virtual bool legacy_page_flip(int fd, std::uint32_t crtc_id,
                                              std::uint32_t framebuffer_id,
                                              PageFlipCookie &cookie,
                                              std::string &error) = 0;
};

class KmsDumbBufferApi final : public DumbBufferApi {
public:
  KmsDumbBufferApi(KmsApi &api, const int fd) : api_(api), fd_(fd) {}
  bool create_dumb(std::uint32_t, std::uint32_t, std::uint32_t,
                   DumbAllocation &, std::string &) override;
  bool add_framebuffer2(std::uint32_t, std::uint32_t, std::uint32_t,
                        std::uint32_t, std::uint32_t, std::uint32_t &,
                        std::string &) override;
  bool map_dumb(std::uint32_t, std::uint64_t &, std::string &) override;
  std::byte *map_memory(std::uint64_t, std::size_t, std::string &) override;
  bool remove_framebuffer(std::uint32_t, std::string &) noexcept override;
  bool unmap_memory(std::byte *, std::size_t, std::string &) noexcept override;
  bool destroy_dumb(std::uint32_t, std::string &) noexcept override;

private:
  KmsApi &api_;
  int fd_;
};

[[nodiscard]] std::unique_ptr<KmsApi> make_real_kms_api();

} // namespace glasswyrm::drm
