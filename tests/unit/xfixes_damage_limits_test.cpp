#include "glasswyrmd/resource_table.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using namespace glasswyrm::server;
using glasswyrm::geometry::Rectangle;
using gw::test::require;

constexpr ClientId kOwner = 7;
constexpr std::uint32_t kBase = 0x400000;
constexpr std::uint32_t kMask = 0x1FFFFF;

void test_injected_counts_and_rectangle_limit() {
  ResourceLimits limits;
  limits.maximum_xfixes_regions_per_client = 3;
  limits.maximum_xfixes_region_rectangles = 1;
  limits.maximum_damage_resources_per_client = 1;
  ResourceTable table(kScreenModel, limits);
  constexpr std::uint32_t first = kBase | 1U;
  constexpr std::uint32_t second = kBase | 2U;
  constexpr std::uint32_t destination = kBase | 3U;
  constexpr std::uint32_t over_count = kBase | 4U;
  constexpr std::uint32_t pixmap = kBase | 10U;
  constexpr std::uint32_t damage = kBase | 11U;
  constexpr std::uint32_t second_damage = kBase | 12U;
  const std::array first_rectangle{Rectangle{0, 0, 1, 1}};
  const std::array second_rectangle{Rectangle{4, 0, 1, 1}};
  const std::array two_rectangles{Rectangle{0, 0, 1, 1},
                                  Rectangle{4, 0, 1, 1}};

  require(table.create_xfixes_region(kOwner, kBase, kMask, first,
                                     first_rectangle) ==
                  RegionStatus::Success &&
              table.create_xfixes_region(kOwner, kBase, kMask, second,
                                          second_rectangle) ==
                  RegionStatus::Success &&
              table.create_xfixes_region(kOwner, kBase, kMask, destination,
                                          {}) == RegionStatus::Success,
          "injected region count admits exactly its configured capacity");
  require(table.create_xfixes_region(kOwner, kBase, kMask, over_count, {}) ==
              RegionStatus::BadAlloc,
          "injected region count rejects one additional resource");
  require(table.set_xfixes_region(destination, two_rectangles) ==
              RegionStatus::BadAlloc &&
              table.find_xfixes_region(destination)->rectangles.empty(),
          "injected rectangle cap rejects SetRegion atomically");
  require(table.combine_xfixes_regions(first, second, destination, 0) ==
              RegionStatus::BadAlloc &&
              table.find_xfixes_region(destination)->rectangles.empty(),
          "injected rectangle cap rejects region algebra atomically");

  require(table.create_pixmap(kOwner, kBase, kMask, pixmap,
                              table.screen().root_window, 24, 8, 8) ==
                  CreatePixmapStatus::Success &&
              table.create_damage(kOwner, kBase, kMask, damage, pixmap,
                                  DamageReportLevel::NonEmpty) ==
                  DamageStatus::Success &&
              table.create_damage(kOwner, kBase, kMask, second_damage, pixmap,
                                  DamageReportLevel::NonEmpty) ==
                  DamageStatus::BadAlloc,
          "injected damage count rejects one additional resource");
  const auto mutation = table.add_damage(pixmap, two_rectangles);
  require(mutation.status == DamageStatus::Success &&
              table.find_damage(damage)->accumulated ==
                  std::vector<Rectangle>{{0, 0, 8, 8}},
          "damage complexity above cap falls back to full drawable bounds");
  require(table.invariants_hold(),
          "injected limits remain reflected in resource invariants");
}

void test_cleanup_and_xid_reuse_stress() {
  ResourceLimits limits;
  limits.maximum_xfixes_regions_per_client = 1;
  limits.maximum_damage_resources_per_client = 1;
  ResourceTable table(kScreenModel, limits);
  constexpr std::uint32_t pixmap = kBase | 20U;
  constexpr std::uint32_t reusable = kBase | 21U;
  const std::array rectangle{Rectangle{0, 0, 1, 1}};
  require(table.create_pixmap(kOwner, kBase, kMask, pixmap,
                              table.screen().root_window, 24, 2, 2) ==
              CreatePixmapStatus::Success,
          "create cleanup stress drawable");
  for (std::size_t iteration = 0; iteration < 64; ++iteration) {
    require(table.create_xfixes_region(kOwner, kBase, kMask, reusable,
                                       rectangle) == RegionStatus::Success &&
                table.destroy_xfixes_region(reusable) ==
                    RegionStatus::Success &&
                table.create_damage(kOwner, kBase, kMask, reusable, pixmap,
                                    DamageReportLevel::BoundingBox) ==
                    DamageStatus::Success &&
                table.destroy_damage(reusable) == DamageStatus::Success,
            "region and damage XID are reusable after explicit destroy");
  }
  require(table.create_xfixes_region(kOwner, kBase, kMask, reusable,
                                     rectangle) == RegionStatus::Success,
          "create reusable resource before owner cleanup");
  const auto cleanup = table.cleanup_client(kOwner);
  require(cleanup.resources_destroyed == 2 && !table.find(reusable) &&
              !table.find(pixmap) && table.invariants_hold(),
          "owner cleanup releases stress resources and indexes");

  constexpr ClientId next_owner = 8;
  require(table.create_xfixes_region(next_owner, kBase, kMask, reusable,
                                     rectangle) == RegionStatus::Success &&
              table.destroy_xfixes_region(reusable) == RegionStatus::Success &&
              table.invariants_hold(),
          "XID can be reused by a later client after cleanup");
}

}  // namespace

int main() {
  test_injected_counts_and_rectangle_limit();
  test_cleanup_and_xid_reuse_stress();
  return 0;
}
