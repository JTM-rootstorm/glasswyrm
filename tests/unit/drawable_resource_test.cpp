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
  gw::test::require(
      table.create_pixmap(owner, base, mask, base | 3U,
                          table.screen().root_window, 1, 9, 3) ==
          CreatePixmapStatus::Success,
      "create depth-one pixmap");
  const auto* bitmap = table.find_pixmap(base | 3U);
  gw::test::require(bitmap != nullptr && bitmap->depth == 1 &&
                        bitmap->bitmap() != nullptr &&
                        bitmap->pixels() == nullptr &&
                        bitmap->byte_size() == 27U,
                    "depth-one storage variant");
  GraphicsContextResource bitmap_gc;
  bitmap_gc.foreground = 3;
  bitmap_gc.background = 2;
  gw::test::require(table.create_gc(owner, base, mask, base | 4U, base | 3U,
                                    bitmap_gc) == CreateGcStatus::Success,
                    "create depth-one gc");
  const auto* stored_bitmap_gc = table.find_gc(base | 4U);
  gw::test::require(stored_bitmap_gc != nullptr &&
                        stored_bitmap_gc->depth == 1 &&
                        stored_bitmap_gc->foreground == 1 &&
                        stored_bitmap_gc->background == 0 &&
                        stored_bitmap_gc->plane_mask == 1,
                    "depth-one gc values canonicalized");
  gw::test::require(table.open_font(owner, base, mask, base | 5U) ==
                        OpenFontStatus::Success &&
                        table.find_font(base | 5U) != nullptr,
                    "open client font for cleanup");
  const auto cleanup = table.cleanup_client(owner);
  gw::test::require(cleanup.resources_destroyed == 5, "cleanup typed resources");
  gw::test::require(table.resource_count(ResourceType::Pixmap) == 0, "pixmap cleanup");
  gw::test::require(table.resource_count(ResourceType::GraphicsContext) == 0,
                    "gc cleanup");
  gw::test::require(table.resource_count(ResourceType::Font) == 1 &&
                        table.find_font(kDefaultFontXid) != nullptr,
                    "client cleanup preserves only server default font");
  gw::test::require(table.canonical_drawable_bytes() == 0, "cleanup accounting");

  ResourceLimits limits;
  limits.maximum_canonical_drawable_bytes = 16;
  limits.maximum_pixmaps = 1;
  limits.maximum_graphics_contexts = 1;
  limits.maximum_fonts_per_client = 1;
  limits.maximum_total_fonts = 1;
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
  gw::test::require(
      bounded.open_font(owner, base, mask, base | 13U) ==
              OpenFontStatus::Success &&
          bounded.open_font(owner, base, mask, base | 14U) ==
              OpenFontStatus::BadAlloc &&
          bounded.find_font(base | 14U) == nullptr,
      "font limit is atomic and excludes server default");
  gw::test::require(
      bounded.close_font(base | 13U) == CloseFontStatus::Success &&
          bounded.open_font(owner, base, mask, base | 14U) ==
              OpenFontStatus::Success,
      "font capacity is released");

  ResourceLimits font_limits;
  font_limits.maximum_fonts_per_client = 1;
  font_limits.maximum_total_fonts = 2;
  ResourceTable font_bounded(kScreenModel, font_limits);
  constexpr ClientId second_owner = 8;
  constexpr std::uint32_t second_base = 0x02400000U;
  gw::test::require(
      font_bounded.open_font(owner, base, mask, base | 20U) ==
              OpenFontStatus::Success &&
          font_bounded.open_font(owner, base, mask, base | 21U) ==
              OpenFontStatus::BadAlloc &&
          font_bounded.open_font(second_owner, second_base, mask,
                                 second_base | 1U) ==
              OpenFontStatus::Success &&
          font_bounded.open_font(9, 0x02800000U, mask, 0x02800001U) ==
              OpenFontStatus::BadAlloc,
      "font limits distinguish per-client and total ownership");
  return 0;
}
