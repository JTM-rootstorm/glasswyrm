#include "backends/drm/fake_kms_api.hpp"
#include <algorithm>
namespace glasswyrm::drm {
bool FakeKmsApi::reject(KmsOperation op, std::string &error) {
  if (failure_ != op)
    return false;
  failure_.reset();
  error = "injected KMS operation failure";
  return true;
}
bool FakeKmsApi::acquire_master(int, std::string &e) {
  calls.push_back("set_master");
  if (reject(KmsOperation::AcquireMaster, e))
    return false;
  master = true;
  return true;
}
bool FakeKmsApi::drop_master(int, std::string &e) {
  calls.push_back("drop_master");
  if (reject(KmsOperation::DropMaster, e))
    return false;
  master = false;
  return true;
}
bool FakeKmsApi::create_dumb(int, std::uint32_t w, std::uint32_t h,
                             std::uint32_t b, DumbAllocation &out,
                             std::string &e) {
  calls.push_back("create_dumb:" + std::to_string(w) + "x" + std::to_string(h) +
                  ":" + std::to_string(b));
  if (reject(KmsOperation::CreateDumb, e))
    return false;
  out = dumb_allocation;
  return true;
}
bool FakeKmsApi::add_framebuffer2(int, std::uint32_t handle, std::uint32_t w,
                                  std::uint32_t h, std::uint32_t pitch,
                                  std::uint32_t format, std::uint32_t &fb,
                                  std::string &e) {
  calls.push_back("addfb2:" + std::to_string(handle) + ":" + std::to_string(w) +
                  "x" + std::to_string(h) + ":" + std::to_string(pitch) + ":" +
                  std::to_string(format));
  if (reject(KmsOperation::AddFb2, e))
    return false;
  fb = next_framebuffer++;
  return true;
}
bool FakeKmsApi::map_dumb(int, std::uint32_t handle, std::uint64_t &off,
                          std::string &e) {
  calls.push_back("map_dumb:" + std::to_string(handle));
  if (reject(KmsOperation::MapDumb, e))
    return false;
  off = 4096;
  return true;
}
std::byte *FakeKmsApi::map_memory(int, std::uint64_t off, std::size_t size,
                                  std::string &e) {
  calls.push_back("mmap:" + std::to_string(off) + ":" + std::to_string(size));
  if (reject(KmsOperation::MapMemory, e))
    return nullptr;
  mappings.emplace_back(size, std::byte{0xa5});
  return mappings.back().data();
}
bool FakeKmsApi::remove_framebuffer(int, std::uint32_t fb,
                                    std::string &error) noexcept {
  calls.push_back("rmfb:" + std::to_string(fb));
  return !reject(KmsOperation::RemoveFramebuffer, error);
}
bool FakeKmsApi::unmap_memory(std::byte *mapping, std::size_t s,
                              std::string &error) noexcept {
  calls.push_back("unmap:" + std::to_string(s));
  if (reject(KmsOperation::UnmapMemory, error))
    return false;
  const auto found =
      std::find_if(mappings.begin(), mappings.end(),
                   [mapping](const auto &m) { return m.data() == mapping; });
  if (found != mappings.end())
    mappings.erase(found);
  return true;
}
bool FakeKmsApi::destroy_dumb(int, std::uint32_t h,
                              std::string &error) noexcept {
  calls.push_back("destroy_dumb:" + std::to_string(h));
  return !reject(KmsOperation::DestroyDumb, error);
}
bool FakeKmsApi::object_properties(int, KmsObjectType t, std::uint32_t id,
                                   std::vector<ObjectProperty> &out,
                                   std::string &e) {
  calls.push_back("properties:" + std::to_string(id));
  if (reject(KmsOperation::Properties, e))
    return false;
  auto it = properties.find({t, id});
  if (it == properties.end()) {
    e = "fake object properties missing";
    return false;
  }
  out = it->second;
  return true;
}
bool FakeKmsApi::read_connector_crtc(int, std::uint32_t id, std::uint32_t &out,
                                     std::string &e) {
  calls.push_back("read_connector:" + std::to_string(id));
  if (reject(KmsOperation::ReadConnector, e))
    return false;
  auto it = connector_crtcs.find(id);
  if (it == connector_crtcs.end()) {
    e = "fake connector missing";
    return false;
  }
  out = it->second;
  return true;
}
bool FakeKmsApi::read_crtc(int, std::uint32_t id, KmsCrtcState &out,
                           std::string &e) {
  calls.push_back("read_crtc:" + std::to_string(id));
  if (reject(KmsOperation::ReadCrtc, e))
    return false;
  auto it = crtcs.find(id);
  if (it == crtcs.end()) {
    e = "fake CRTC missing";
    return false;
  }
  out = it->second;
  return true;
}
bool FakeKmsApi::read_plane(int, std::uint32_t id, KmsPlaneState &out,
                            std::string &e) {
  calls.push_back("read_plane:" + std::to_string(id));
  if (reject(KmsOperation::ReadPlane, e))
    return false;
  auto it = planes.find(id);
  if (it == planes.end()) {
    e = "fake plane missing";
    return false;
  }
  out = it->second;
  return true;
}
bool FakeKmsApi::create_mode_blob(int, const KmsMode &, std::uint32_t &id,
                                  std::string &e) {
  calls.push_back("create_blob");
  if (reject(KmsOperation::CreateBlob, e))
    return false;
  id = next_blob++;
  return true;
}
void FakeKmsApi::destroy_mode_blob(int, std::uint32_t id) noexcept {
  calls.push_back("destroy_blob:" + std::to_string(id));
}
bool FakeKmsApi::atomic_commit(int, std::span<const AtomicPropertyValue> p,
                               std::uint32_t flags, PageFlipCookie *cookie,
                               std::string &e) {
  calls.push_back("atomic:" + std::to_string(flags));
  if (reject(KmsOperation::AtomicCommit, e))
    return false;
  atomic_commits.push_back({{p.begin(), p.end()}, flags, cookie});
  if (atomic_result_connector_crtc && !connector_crtcs.empty())
    connector_crtcs.begin()->second = *atomic_result_connector_crtc;
  if (atomic_result_crtc)
    crtcs[atomic_result_crtc->crtc_id] = *atomic_result_crtc;
  if (atomic_result_plane)
    planes[atomic_result_plane->plane_id] = *atomic_result_plane;
  return true;
}
bool FakeKmsApi::legacy_set_crtc(int, const KmsCrtcState &s,
                                 std::span<const std::uint32_t> connectors,
                                 std::string &e) {
  calls.push_back("setcrtc:" + std::to_string(s.crtc_id) + ":" +
                  std::to_string(s.framebuffer_id) + ":" +
                  std::to_string(connectors.size()));
  if (reject(KmsOperation::LegacySetCrtc, e))
    return false;
  legacy_modesets.push_back(s);
  crtcs[s.crtc_id] = s;
  for (auto id : connectors)
    connector_crtcs[id] = s.active ? s.crtc_id : 0;
  return true;
}
bool FakeKmsApi::legacy_page_flip(int, std::uint32_t crtc, std::uint32_t fb,
                                  PageFlipCookie &cookie, std::string &e) {
  calls.push_back("pageflip:" + std::to_string(crtc) + ":" +
                  std::to_string(fb) + ":" + std::to_string(cookie.token));
  return !reject(KmsOperation::LegacyPageFlip, e);
}
} // namespace glasswyrm::drm
