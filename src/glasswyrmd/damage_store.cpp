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

DamageMutationResult ResourceTable::subtract_damage(
    const std::uint32_t xid, const std::uint32_t repair_region,
    const std::uint32_t parts_region) {
  auto* damage = find_damage(xid);
  if (!damage) return {DamageStatus::BadDamage, {}};
  const XFixesRegionResource* repair = nullptr;
  XFixesRegionResource* parts = nullptr;
  if (repair_region != 0) {
    repair = find_xfixes_region(repair_region);
    if (!repair) return {DamageStatus::BadValue, {}};
  }
  if (parts_region != 0) {
    parts = find_xfixes_region(parts_region);
    if (!parts) return {DamageStatus::BadValue, {}};
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
    if (!repaired || !remaining ||
        repaired->size() > limits_.maximum_xfixes_region_rectangles ||
        remaining->size() > limits_.maximum_xfixes_region_rectangles)
      return {DamageStatus::BadAlloc, {}};
    std::vector<DamageNotification> notifications;
    if (!remaining->empty()) {
      const auto* record = find(xid);
      const auto drawable_bounds = drawable_geometry(*this, damage->drawable);
      if (!record || !record->owner || !drawable_bounds)
        return {DamageStatus::BadDamage, {}};
      notifications.push_back(
          {*record->owner, xid, damage->drawable, damage->level,
           region_extents(*remaining), *drawable_bounds});
    }
    if (parts) parts->rectangles = std::move(*repaired);
    damage->accumulated = std::move(*remaining);
    damage->non_empty_event_sent = !damage->accumulated.empty();
    return {DamageStatus::Success, std::move(notifications)};
  } catch (const std::bad_alloc&) {
    return {DamageStatus::BadAlloc, {}};
  }
}

DamageMutationResult ResourceTable::add_damage(
    const std::uint32_t drawable,
    const std::span<const geometry::Rectangle> rectangles) {
  const auto geometry = drawable_geometry(*this, drawable);
  if (!geometry) return {DamageStatus::BadDrawable, {}};
  try {
    std::vector<geometry::Rectangle> clipped;
    clipped.reserve(rectangles.size());
    for (const auto rectangle : rectangles)
      if (const auto overlap = glasswyrm::geometry::intersect(*geometry,
                                                              rectangle))
        clipped.push_back(*overlap);
    auto incoming = normalize_region(clipped);
    if (!incoming) return {DamageStatus::BadAlloc, {}};
    if (incoming->empty()) return {};

    struct StagedDamage {
      DamageResource* resource{};
      std::vector<geometry::Rectangle> accumulated;
      std::optional<DamageNotification> notification;
    };
    std::vector<StagedDamage> staged;
    for (auto& [xid, record] : resources_) {
      auto* damage = std::get_if<DamageResource>(&record.payload);
      if (!damage || damage->drawable != drawable || !record.owner) continue;
      auto accumulated = union_regions(damage->accumulated, *incoming);
      if (!accumulated) return {DamageStatus::BadAlloc, {}};
      if (accumulated->size() > limits_.maximum_xfixes_region_rectangles) {
        if (limits_.maximum_xfixes_region_rectangles == 0)
          return {DamageStatus::BadAlloc, {}};
        accumulated = std::vector<geometry::Rectangle>{*geometry};
      }
      const auto previous_bounds = region_extents(damage->accumulated);
      const auto next_bounds = region_extents(*accumulated);
      const bool notify = damage->level == DamageReportLevel::NonEmpty
                              ? damage->accumulated.empty()
                              : previous_bounds != next_bounds;
      StagedDamage item{damage, std::move(*accumulated), std::nullopt};
      if (notify)
        item.notification = DamageNotification{
            *record.owner, xid, drawable, damage->level,
            damage->level == DamageReportLevel::BoundingBox
                ? next_bounds
                : region_extents(*incoming),
            *geometry};
      staged.push_back(std::move(item));
    }
    std::vector<DamageNotification> notifications;
    notifications.reserve(staged.size());
    for (auto& item : staged)
      if (item.notification) notifications.push_back(*item.notification);
    for (auto& item : staged) {
      item.resource->accumulated = std::move(item.accumulated);
      item.resource->non_empty_event_sent = true;
    }
    std::ranges::sort(notifications, {}, &DamageNotification::damage);
    return {DamageStatus::Success, std::move(notifications)};
  } catch (const std::bad_alloc&) {
    return {DamageStatus::BadAlloc, {}};
  }
}

std::vector<DamageNotification> ResourceTable::damage_drawable(
    const std::uint32_t drawable, const geometry::Rectangle rectangle) {
  const auto result = add_damage(
      drawable, std::span<const geometry::Rectangle>(&rectangle, 1));
  return result.status == DamageStatus::Success ? result.notifications
                                                 : std::vector<DamageNotification>{};
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
