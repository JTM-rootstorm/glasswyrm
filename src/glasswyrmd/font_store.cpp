#include "glasswyrmd/resource_table.hpp"

#include <algorithm>
#include <new>

namespace glasswyrm::server {

OpenFontStatus ResourceTable::open_font(
    const ClientId owner, const std::uint32_t resource_base,
    const std::uint32_t resource_mask, const std::uint32_t xid) {
  if (!valid_new_resource_id(xid, resource_base, resource_mask))
    return OpenFontStatus::BadIdChoice;
  std::size_t client_fonts = 0;
  std::size_t total_fonts = 0;
  for (const auto& [resource_xid, record] : resources_) {
    static_cast<void>(resource_xid);
    if (record.type != ResourceType::Font || !record.owner) continue;
    ++total_fonts;
    if (*record.owner == owner) ++client_fonts;
  }
  if (client_fonts >= limits_.maximum_fonts_per_client ||
      total_fonts >= limits_.maximum_total_fonts)
    return OpenFontStatus::BadAlloc;
  try {
    resources_.emplace(xid, ResourceRecord{ResourceType::Font, owner,
                                           FontResource{}});
    try { resources_by_owner_[owner].push_back(xid); }
    catch (...) { resources_.erase(xid); throw; }
  } catch (const std::bad_alloc&) { return OpenFontStatus::BadAlloc; }
  return OpenFontStatus::Success;
}

CloseFontStatus ResourceTable::close_font(const std::uint32_t xid) {
  const auto* font = find_font(xid);
  const auto* record = find(xid);
  if (!font || !record || !record->owner) return CloseFontStatus::BadFont;
  const auto owner = *record->owner;
  resources_.erase(xid);
  auto iterator = resources_by_owner_.find(owner);
  if (iterator != resources_by_owner_.end()) {
    std::erase(iterator->second, xid);
    if (iterator->second.empty()) resources_by_owner_.erase(iterator);
  }
  return CloseFontStatus::Success;
}

}  // namespace glasswyrm::server
