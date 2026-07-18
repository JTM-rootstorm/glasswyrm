#include "glasswyrmd/resource_table.hpp"

#include <algorithm>
#include <new>

namespace glasswyrm::server {

CreateColormapStatus ResourceTable::create_colormap(
    const ClientId owner, const std::uint32_t resource_base,
    const std::uint32_t resource_mask, const std::uint32_t xid,
    const std::uint32_t window, const std::uint32_t visual) {
  if (!valid_new_resource_id(xid, resource_base, resource_mask))
    return CreateColormapStatus::BadIdChoice;
  if (!find_window(window)) return CreateColormapStatus::BadWindow;
  if (visual != screen_.root_visual) return CreateColormapStatus::BadMatch;
  const auto owned_count = std::ranges::count_if(
      resources_, [owner](const auto& entry) {
        return entry.second.owner == owner &&
               entry.second.type == ResourceType::Colormap;
      });
  if (static_cast<std::size_t>(owned_count) >=
      limits_.maximum_colormaps_per_client)
    return CreateColormapStatus::BadAlloc;
  try {
    resources_.emplace(
        xid, ResourceRecord{ResourceType::Colormap, owner,
                            ColormapResource{visual}});
    try {
      resources_by_owner_[owner].push_back(xid);
    } catch (...) {
      resources_.erase(xid);
      throw;
    }
  } catch (const std::bad_alloc&) {
    return CreateColormapStatus::BadAlloc;
  }
  return CreateColormapStatus::Success;
}

FreeColormapStatus ResourceTable::free_colormap(const std::uint32_t xid) {
  if (xid == screen_.default_colormap) return FreeColormapStatus::BadAccess;
  const auto* record = find(xid);
  if (!record || record->type != ResourceType::Colormap)
    return FreeColormapStatus::BadColormap;
  const auto owner = *record->owner;
  resources_.erase(xid);
  auto owner_iterator = resources_by_owner_.find(owner);
  if (owner_iterator != resources_by_owner_.end()) {
    std::erase(owner_iterator->second, xid);
    if (owner_iterator->second.empty()) resources_by_owner_.erase(owner_iterator);
  }
  return FreeColormapStatus::Success;
}

}  // namespace glasswyrm::server
