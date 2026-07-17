#include "glasswyrmd/resource_table.hpp"

#include <algorithm>
#include <new>

namespace glasswyrm::server {
namespace {

std::size_t owned_damage(
    const std::unordered_map<std::uint32_t, ResourceRecord>& resources,
    const ClientId owner) {
  return static_cast<std::size_t>(std::ranges::count_if(
      resources, [owner](const auto& entry) {
        return entry.second.owner == owner &&
               entry.second.type == ResourceType::Damage;
      }));
}

std::optional<geometry::Rectangle> drawable_geometry(
    const ResourceTable& resources, const std::uint32_t drawable) {
  if (const auto* window = resources.find_window(drawable))
    return geometry::Rectangle{0, 0, window->width, window->height};
  if (const auto* pixmap = resources.find_pixmap(drawable))
    return geometry::Rectangle{0, 0, pixmap->width, pixmap->height};
  return std::nullopt;
}

void remove_owned_id(
    std::unordered_map<ClientId, std::vector<std::uint32_t>>& owners,
    const ClientId owner, const std::uint32_t xid) {
  const auto found = owners.find(owner);
  if (found == owners.end()) return;
  std::erase(found->second, xid);
  if (found->second.empty()) owners.erase(found);
}

}  // namespace

DamageStatus ResourceTable::create_damage(
    const ClientId owner, const std::uint32_t resource_base,
    const std::uint32_t resource_mask, const std::uint32_t xid,
    const std::uint32_t drawable, const DamageReportLevel level) {
  if (!valid_new_resource_id(xid, resource_base, resource_mask))
    return DamageStatus::BadIdChoice;
  if (!drawable_geometry(*this, drawable)) return DamageStatus::BadDrawable;
  if (owned_damage(resources_, owner) >=
      limits_.maximum_damage_resources_per_client)
    return DamageStatus::BadAlloc;
  try {
    resources_.emplace(
        xid, ResourceRecord{ResourceType::Damage, owner,
                            DamageResource{drawable, level, {}, false}});
    try {
      resources_by_owner_[owner].push_back(xid);
    } catch (...) {
      resources_.erase(xid);
      throw;
    }
  } catch (const std::bad_alloc&) {
    return DamageStatus::BadAlloc;
  }
  return DamageStatus::Success;
}

DamageStatus ResourceTable::destroy_damage(const std::uint32_t xid) {
  if (!find_damage(xid)) return DamageStatus::BadDamage;
  const auto owner = *find(xid)->owner;
  resources_.erase(xid);
  remove_owned_id(resources_by_owner_, owner, xid);
  return DamageStatus::Success;
}

DamageStatus ResourceTable::subtract_damage(const std::uint32_t xid,
                                            const std::uint32_t repair_region,
                                            const std::uint32_t parts_region) {
  auto* damage = find_damage(xid);
  if (!damage) return DamageStatus::BadDamage;
  const XFixesRegionResource* repair = nullptr;
  XFixesRegionResource* parts = nullptr;
  if (repair_region != 0) {
    repair = find_xfixes_region(repair_region);
    if (!repair) return DamageStatus::BadValue;
  }
  if (parts_region != 0) {
    parts = find_xfixes_region(parts_region);
    if (!parts) return DamageStatus::BadValue;
  }
  try {
    std::optional<std::vector<geometry::Rectangle>> repaired;
    std::optional<std::vector<geometry::Rectangle>> remaining;
    if (repair) {
      repaired = intersect_regions(damage->accumulated, repair->rectangles);
      remaining = subtract_regions(damage->accumulated, repair->rectangles);
    } else {
      repaired = damage->accumulated;
      remaining = std::vector<geometry::Rectangle>{};
    }
    if (!repaired || !remaining) return DamageStatus::BadAlloc;
    if (parts) parts->rectangles = std::move(*repaired);
    damage->accumulated = std::move(*remaining);
    if (damage->accumulated.empty()) damage->non_empty_event_sent = false;
  } catch (const std::bad_alloc&) {
    return DamageStatus::BadAlloc;
  }
  return DamageStatus::Success;
}

DamageStatus ResourceTable::add_damage(const std::uint32_t drawable,
                                       const std::uint32_t region) {
  if (!drawable_geometry(*this, drawable)) return DamageStatus::BadDrawable;
  if (!find_xfixes_region(region)) return DamageStatus::BadValue;
  return DamageStatus::Success;
}

std::vector<DamageNotification> ResourceTable::damage_drawable(
    const std::uint32_t drawable, const geometry::Rectangle rectangle) {
  std::vector<DamageNotification> notifications;
  const auto geometry = drawable_geometry(*this, drawable);
  const auto clipped = geometry ? glasswyrm::geometry::intersect(*geometry,
                                                                 rectangle)
                                : std::nullopt;
  if (!clipped) return notifications;
  for (auto& [xid, record] : resources_) {
    auto* damage = std::get_if<DamageResource>(&record.payload);
    if (!damage || damage->drawable != drawable || !record.owner) continue;
    const bool was_empty = damage->accumulated.empty();
    auto accumulated = union_regions(damage->accumulated,
                                     std::span<const geometry::Rectangle>(
                                         &*clipped, 1));
    if (accumulated)
      damage->accumulated = std::move(*accumulated);
    else
      damage->accumulated = {*geometry};
    if (damage->level == DamageReportLevel::NonEmpty && !was_empty) continue;
    damage->non_empty_event_sent = true;
    notifications.push_back(
        {*record.owner, xid, drawable, damage->level,
         damage->level == DamageReportLevel::BoundingBox
             ? region_extents(damage->accumulated)
             : *clipped,
         *geometry});
  }
  std::ranges::sort(notifications, {}, &DamageNotification::damage);
  return notifications;
}

std::size_t ResourceTable::remove_damage_for_drawable(
    const std::uint32_t drawable) {
  std::vector<std::uint32_t> removed;
  for (const auto& [xid, record] : resources_)
    if (const auto* damage = std::get_if<DamageResource>(&record.payload);
        damage && damage->drawable == drawable)
      removed.push_back(xid);
  for (const auto xid : removed) (void)destroy_damage(xid);
  return removed.size();
}

}  // namespace glasswyrm::server
