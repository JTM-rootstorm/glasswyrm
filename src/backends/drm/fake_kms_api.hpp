#pragma once
#include "backends/drm/kms_api.hpp"
#include <list>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace glasswyrm::drm {
enum class KmsOperation {
  CheckMaster,
  AcquireMaster,
  DropMaster,
  CreateDumb,
  AddFb2,
  MapDumb,
  MapMemory,
  Properties,
  ReadConnector,
  ReadCrtc,
  ReadPlane,
  CreateBlob,
  AtomicCommit,
  LegacySetCrtc,
  LegacyPageFlip,
  RemoveFramebuffer,
  UnmapMemory,
  DestroyDumb
};
struct AtomicCommitRecord {
  std::vector<AtomicPropertyValue> properties;
  std::uint32_t flags{};
  PageFlipCookie *cookie{};
};
class FakeKmsApi final : public KmsApi {
public:
  bool master{};
  DumbAllocation dumb_allocation{7, 12, 24};
  std::uint32_t next_framebuffer{70}, next_blob{90};
  std::list<std::vector<std::byte>> mappings;
  std::vector<std::string> calls;
  std::map<std::pair<KmsObjectType, std::uint32_t>, std::vector<ObjectProperty>>
      properties;
  std::map<std::uint32_t, std::uint32_t> connector_crtcs;
  std::map<std::uint32_t, KmsCrtcState> crtcs;
  std::map<std::uint32_t, KmsPlaneState> planes;
  std::vector<AtomicCommitRecord> atomic_commits;
  std::vector<KmsCrtcState> legacy_modesets;
  std::optional<KmsCrtcState> atomic_result_crtc;
  std::optional<KmsPlaneState> atomic_result_plane;
  std::optional<std::uint32_t> atomic_result_connector_crtc;
  std::optional<std::pair<std::uint32_t, std::uint64_t>>
      rejected_test_property;
  std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint64_t>
      property_readback_overrides;

  void fail_next(KmsOperation operation) { failure_ = operation; }
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

private:
  bool reject(KmsOperation, std::string &);
  std::optional<KmsOperation> failure_;
};
} // namespace glasswyrm::drm
