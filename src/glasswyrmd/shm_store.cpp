#include "glasswyrmd/resource_table.hpp"

#include <sys/shm.h>

#include <algorithm>
#include <limits>
#include <new>

namespace glasswyrm::server {

SysvShmMapping::~SysvShmMapping() {
  if (address_) (void)::shmdt(address_);
}

AttachShmStatus ResourceTable::attach_shm_segment(
    const ClientId owner, const std::uint32_t resource_base,
    const std::uint32_t resource_mask, const std::uint32_t xid,
    const std::uint32_t shmid_wire, const bool read_only,
    const std::uint32_t peer_uid) {
  if (!valid_new_resource_id(xid, resource_base, resource_mask))
    return AttachShmStatus::BadIdChoice;
  if (shmid_wire > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
    return AttachShmStatus::BadValue;
  const auto shmid = static_cast<int>(shmid_wire);
  struct shmid_ds status {};
  if (::shmctl(shmid, IPC_STAT, &status) < 0 || status.shm_segsz == 0)
    return AttachShmStatus::BadAccess;
#ifdef SHM_DEST
  if ((status.shm_perm.mode & SHM_DEST) != 0)
    return AttachShmStatus::BadAccess;
#endif
  if (status.shm_perm.uid != peer_uid && status.shm_perm.cuid != peer_uid)
    return AttachShmStatus::BadAccess;

  std::size_t owned_count = 0;
  std::size_t owned_bytes = 0;
  for (const auto& [resource_xid, record] : resources_) {
    static_cast<void>(resource_xid);
    if (record.owner != owner || record.type != ResourceType::ShmSegment)
      continue;
    const auto* segment = std::get_if<ShmSegmentResource>(&record.payload);
    if (!segment || segment->size > limits_.maximum_shm_bytes_per_client ||
        owned_bytes >
            limits_.maximum_shm_bytes_per_client - segment->size)
      return AttachShmStatus::BadAlloc;
    ++owned_count;
    owned_bytes += segment->size;
  }
  const auto size = static_cast<std::size_t>(status.shm_segsz);
  if (owned_count >= limits_.maximum_shm_segments_per_client ||
      size > limits_.maximum_shm_bytes_per_client - owned_bytes)
    return AttachShmStatus::BadAlloc;

  void* address = ::shmat(shmid, nullptr, read_only ? SHM_RDONLY : 0);
  if (address == reinterpret_cast<void*>(-1)) return AttachShmStatus::BadAccess;
  std::shared_ptr<SysvShmMapping> mapping;
  try {
    mapping = std::make_shared<SysvShmMapping>(address, size);
  } catch (const std::bad_alloc&) {
    (void)::shmdt(address);
    return AttachShmStatus::BadAlloc;
  }
  try {
    resources_.emplace(
        xid, ResourceRecord{ResourceType::ShmSegment, owner,
                            ShmSegmentResource{shmid, size, read_only, peer_uid,
                                               std::move(mapping)}});
    try {
      resources_by_owner_[owner].push_back(xid);
    } catch (...) {
      resources_.erase(xid);
      throw;
    }
  } catch (const std::bad_alloc&) {
    return AttachShmStatus::BadAlloc;
  }
  return AttachShmStatus::Success;
}

DetachShmStatus ResourceTable::detach_shm_segment(const std::uint32_t xid) {
  const auto* segment = find_shm_segment(xid);
  if (!segment) return DetachShmStatus::BadSegment;
  const auto owner = find(xid)->owner;
  resources_.erase(xid);
  if (owner) {
    auto iterator = resources_by_owner_.find(*owner);
    if (iterator != resources_by_owner_.end()) {
      std::erase(iterator->second, xid);
      if (iterator->second.empty()) resources_by_owner_.erase(iterator);
    }
  }
  return DetachShmStatus::Success;
}

}  // namespace glasswyrm::server
