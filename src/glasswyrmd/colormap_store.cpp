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
    insert_resource(
        xid,
        ResourceRecord{ResourceType::Colormap, owner, ColormapResource{visual}});
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
  erase_resource(xid);
  return FreeColormapStatus::Success;
}

}  // namespace glasswyrm::server
