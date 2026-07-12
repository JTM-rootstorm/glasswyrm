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
  const auto cleanup = table.cleanup_client(owner);
  gw::test::require(cleanup.resources_destroyed == 2, "cleanup typed resources");
  gw::test::require(table.resource_count(ResourceType::Pixmap) == 0, "pixmap cleanup");
  gw::test::require(table.resource_count(ResourceType::GraphicsContext) == 0,
                    "gc cleanup");
  return 0;
}
