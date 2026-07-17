#include "glasswyrmd/resource_table.hpp"

#include <algorithm>
#include <new>

namespace glasswyrm::server {
namespace {

std::size_t owned_regions(
    const std::unordered_map<std::uint32_t, ResourceRecord>& resources,
    const ClientId owner) {
  return static_cast<std::size_t>(std::ranges::count_if(
      resources, [owner](const auto& entry) {
        return entry.second.owner == owner &&
               entry.second.type == ResourceType::XFixesRegion;
      }));
}

}  // namespace

RegionStatus ResourceTable::create_xfixes_region(
    const ClientId owner, const std::uint32_t resource_base,
    const std::uint32_t resource_mask, const std::uint32_t xid,
    const std::span<const geometry::Rectangle> rectangles) {
  if (!valid_new_resource_id(xid, resource_base, resource_mask))
    return RegionStatus::BadIdChoice;
  if (owned_regions(resources_, owner) >=
      limits_.maximum_xfixes_regions_per_client)
    return RegionStatus::BadAlloc;
  auto normalized = normalize_region(rectangles);
  if (!normalized) return RegionStatus::BadValue;
  try {
    resources_.emplace(
        xid, ResourceRecord{ResourceType::XFixesRegion, owner,
                            XFixesRegionResource{std::move(*normalized)}});
    try {
      resources_by_owner_[owner].push_back(xid);
    } catch (...) {
      resources_.erase(xid);
      throw;
    }
  } catch (const std::bad_alloc&) {
    return RegionStatus::BadAlloc;
  }
  return RegionStatus::Success;
}

RegionStatus ResourceTable::destroy_xfixes_region(const std::uint32_t xid) {
  if (!find_xfixes_region(xid)) return RegionStatus::BadRegion;
  const auto owner = *find(xid)->owner;
  resources_.erase(xid);
  if (auto found = resources_by_owner_.find(owner);
      found != resources_by_owner_.end()) {
    std::erase(found->second, xid);
    if (found->second.empty()) resources_by_owner_.erase(found);
  }
  return RegionStatus::Success;
}

RegionStatus ResourceTable::set_xfixes_region(
    const std::uint32_t xid,
    const std::span<const geometry::Rectangle> rectangles) {
  auto* region = find_xfixes_region(xid);
  if (!region) return RegionStatus::BadRegion;
  try {
    auto normalized = normalize_region(rectangles);
    if (!normalized) return RegionStatus::BadValue;
    region->rectangles = std::move(*normalized);
  } catch (const std::bad_alloc&) {
    return RegionStatus::BadAlloc;
  }
  return RegionStatus::Success;
}

RegionStatus ResourceTable::copy_xfixes_region(
    const std::uint32_t source, const std::uint32_t destination) {
  const auto* source_region = find_xfixes_region(source);
  auto* destination_region = find_xfixes_region(destination);
  if (!source_region || !destination_region) return RegionStatus::BadRegion;
  try {
    const auto copy = source_region->rectangles;
    destination_region->rectangles = copy;
  } catch (const std::bad_alloc&) {
    return RegionStatus::BadAlloc;
  }
  return RegionStatus::Success;
}

RegionStatus ResourceTable::combine_xfixes_regions(
    const std::uint32_t source1, const std::uint32_t source2,
    const std::uint32_t destination, const std::uint8_t operation) {
  const auto* first = find_xfixes_region(source1);
  const auto* second = find_xfixes_region(source2);
  auto* output = find_xfixes_region(destination);
  if (!first || !second || !output) return RegionStatus::BadRegion;
  try {
    std::optional<std::vector<geometry::Rectangle>> result;
    if (operation == 0)
      result = union_regions(first->rectangles, second->rectangles);
    else if (operation == 1)
      result = intersect_regions(first->rectangles, second->rectangles);
    else if (operation == 2)
      result = subtract_regions(first->rectangles, second->rectangles);
    else
      return RegionStatus::BadValue;
    if (!result) return RegionStatus::BadAlloc;
    output->rectangles = std::move(*result);
  } catch (const std::bad_alloc&) {
    return RegionStatus::BadAlloc;
  }
  return RegionStatus::Success;
}

RegionStatus ResourceTable::translate_xfixes_region(
    const std::uint32_t xid, const std::int16_t dx, const std::int16_t dy) {
  auto* region = find_xfixes_region(xid);
  if (!region) return RegionStatus::BadRegion;
  try {
    auto translated = translate_region(region->rectangles, dx, dy);
    if (!translated) return RegionStatus::BadValue;
    region->rectangles = std::move(*translated);
  } catch (const std::bad_alloc&) {
    return RegionStatus::BadAlloc;
  }
  return RegionStatus::Success;
}

RegionStatus ResourceTable::extents_xfixes_region(
    const std::uint32_t source, const std::uint32_t destination) {
  const auto* source_region = find_xfixes_region(source);
  auto* output = find_xfixes_region(destination);
  if (!source_region || !output) return RegionStatus::BadRegion;
  try {
    const auto extents = region_extents(source_region->rectangles);
    std::vector<geometry::Rectangle> result;
    if (extents.width != 0 && extents.height != 0) result.push_back(extents);
    output->rectangles = std::move(result);
  } catch (const std::bad_alloc&) {
    return RegionStatus::BadAlloc;
  }
  return RegionStatus::Success;
}

}  // namespace glasswyrm::server
