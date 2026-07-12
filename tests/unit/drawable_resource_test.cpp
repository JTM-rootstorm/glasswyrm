#include "glasswyrmd/resource_table.hpp"
#include "helpers/test_support.hpp"

int main() {
  using namespace glasswyrm::server;
  ResourceTable table;
  constexpr ClientId owner = 7;
  constexpr std::uint32_t base = 0x02000000U, mask = 0x001fffffU;
  const auto pixmap = table.create_pixmap(owner, base, mask, base | 1U,
                                          table.screen().root_window, 24, 8, 4);
  gw::test::require(pixmap == CreatePixmapStatus::Success, "create pixmap");
  gw::test::require(table.find_pixmap(base | 1U) != nullptr, "typed pixmap lookup");
  gw::test::require(table.find_window(base | 1U) == nullptr, "not a window");
  GraphicsContextResource gc;
  const auto context = table.create_gc(owner, base, mask, base | 2U,
                                       base | 1U, gc);
  gw::test::require(context == CreateGcStatus::Success, "create gc");
  gw::test::require(table.find_gc(base | 2U) != nullptr, "typed gc lookup");
  gw::test::require(table.create_pixmap(owner, base, mask, base | 2U,
      table.screen().root_window, 24, 1, 1) == CreatePixmapStatus::BadIdChoice,
      "shared xid namespace");
  gw::test::require(table.invariants_hold(), "resource invariants");
  gw::test::require(table.canonical_drawable_bytes() == 128, "pixmap accounting");
  const auto cleanup = table.cleanup_client(owner);
  gw::test::require(cleanup.resources_destroyed == 2, "cleanup typed resources");
  gw::test::require(table.resource_count(ResourceType::Pixmap) == 0, "pixmap cleanup");
  gw::test::require(table.resource_count(ResourceType::GraphicsContext) == 0,
                    "gc cleanup");
  gw::test::require(table.canonical_drawable_bytes() == 0, "cleanup accounting");

  ResourceLimits limits;
  limits.maximum_canonical_drawable_bytes = 16;
  limits.maximum_pixmaps = 1;
  limits.maximum_graphics_contexts = 1;
  ResourceTable bounded(kScreenModel, limits);
  gw::test::require(bounded.create_pixmap(owner, base, mask, base|10,
      bounded.screen().root_window, 24, 2, 2) == CreatePixmapStatus::Success,
      "bounded pixmap");
  gw::test::require(bounded.create_pixmap(owner, base, mask, base|11,
      bounded.screen().root_window, 24, 1, 1) == CreatePixmapStatus::BadAlloc,
      "pixmap count limit");
  gw::test::require(bounded.free_pixmap(base|10) == FreePixmapStatus::Success &&
      bounded.canonical_drawable_bytes() == 0, "bounded release");
  gw::test::require(bounded.create_pixmap(owner, base, mask, base|12,
      bounded.screen().root_window, 24, 3, 2) == CreatePixmapStatus::BadAlloc,
      "pixmap byte limit is atomic");
  return 0;
}
