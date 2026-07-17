#include "glasswyrmd/resource_table.hpp"

#include <algorithm>
#include <new>
#include <variant>

namespace glasswyrm::server {

PictureResourceStatus ResourceTable::create_picture(
    const ClientId owner, const std::uint32_t resource_base,
    const std::uint32_t resource_mask, const std::uint32_t xid,
    Picture picture) {
  if (!valid_new_resource_id(xid, resource_base, resource_mask))
    return PictureResourceStatus::BadIdChoice;
  std::size_t owned_pictures = 0;
  if (const auto found = resources_by_owner_.find(owner);
      found != resources_by_owner_.end())
    for (const auto owned : found->second)
      if (find_picture(owned)) ++owned_pictures;
  if (owned_pictures >= limits_.maximum_pictures_per_client)
    return PictureResourceStatus::BadAlloc;
  try {
    resources_.emplace(
        xid, ResourceRecord{ResourceType::Picture, owner, std::move(picture)});
    try {
      resources_by_owner_[owner].push_back(xid);
    } catch (...) {
      resources_.erase(xid);
      throw;
    }
  } catch (const std::bad_alloc&) {
    return PictureResourceStatus::BadAlloc;
  }
  return PictureResourceStatus::Success;
}

PictureResourceStatus ResourceTable::free_picture(const std::uint32_t xid) {
  if (!find_picture(xid)) return PictureResourceStatus::BadPicture;
  const auto owner = find(xid)->owner;
  resources_.erase(xid);
  if (owner) {
    auto found = resources_by_owner_.find(*owner);
    if (found != resources_by_owner_.end()) {
      std::erase(found->second, xid);
      if (found->second.empty()) resources_by_owner_.erase(found);
    }
  }
  return PictureResourceStatus::Success;
}

std::size_t ResourceTable::remove_pictures_for_drawable(
    const std::uint32_t drawable) {
  std::vector<std::uint32_t> doomed;
  for (const auto& [xid, resource] : resources_)
    if (const auto* picture = std::get_if<Picture>(&resource.payload))
      if (const auto* source =
              std::get_if<DrawablePictureSource>(&picture->source());
          source && source->drawable == drawable)
        doomed.push_back(xid);
  for (const auto xid : doomed) (void)free_picture(xid);
  return doomed.size();
}

}  // namespace glasswyrm::server
