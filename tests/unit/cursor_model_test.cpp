#include "input/cursor_model.hpp"

#include "helpers/test_support.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace glasswyrm::input;
using gw::test::require;

namespace {

CursorGlyphSpec glyph(const std::uint16_t source) {
  return {CursorFontIdentity::Cursor, CursorFontIdentity::Cursor, source,
          static_cast<std::uint16_t>(source + 1U),
          {0xffff, 0, 0}, {0, 0, 0xffff}};
}

}  // namespace

int main() {
  std::string error;
  const std::array<std::uint8_t, 4> source{1, 0, 0, 1};
  const std::array<std::uint8_t, 4> mask{1, 1, 0, 1};
  CursorPixmapSpec pixmap{10, 11, 2, 2, 1, 1, source, mask,
                          {0xffff, 0, 0}, {0, 0xffff, 0}};
  auto image = make_pixmap_cursor(pixmap, error);
  require(image && error.empty() && image->source_pixmap == 10 &&
              image->mask_pixmap == 11 && image->hotspot_x == 1 &&
              image->hotspot_y == 1 &&
              image->premultiplied_argb ==
                  std::vector<std::uint32_t>{0xffff0000, 0xff00ff00, 0,
                                             0xffff0000},
          "depth-1 source and mask rasterize to premultiplied ARGB");
  pixmap.hotspot_x = 2;
  require(!make_pixmap_cursor(pixmap, error) &&
              error.find("hotspot") != std::string::npos,
          "pixmap cursor hotspot is checked");
  pixmap.hotspot_x = 1;
  const std::array<std::uint8_t, 3> short_mask{};
  pixmap.mask_bits = short_mask;
  require(!make_pixmap_cursor(pixmap, error) && !error.empty(),
          "source and mask dimensions are checked");

  const std::array<std::pair<std::uint16_t, CursorKind>, 11> builtins{{
      {kCursorGlyphLeftPointer, CursorKind::LeftPointer},
      {kCursorGlyphXterm, CursorKind::XtermText},
      {kCursorGlyphFleur, CursorKind::FleurMove},
      {kCursorGlyphBottomRightCorner, CursorKind::BottomRightResize},
      {kCursorGlyphHorizontalDoubleArrow, CursorKind::HorizontalResize},
      {kCursorGlyphScrollDown, CursorKind::VerticalResize},
      {kCursorGlyphScrollLeft, CursorKind::HorizontalResize},
      {kCursorGlyphScrollRight, CursorKind::HorizontalResize},
      {kCursorGlyphScrollUp, CursorKind::VerticalResize},
      {kCursorGlyphVerticalDoubleArrow, CursorKind::VerticalResize},
      {kCursorGlyphWatch, CursorKind::Watch},
  }};
  require(cursor_kind_name(CursorKind::Pixmap) == "pixmap" &&
              cursor_kind_name(CursorKind::LeftPointer) == "left-pointer" &&
              cursor_kind_name(CursorKind::XtermText) == "xterm-text" &&
              cursor_kind_name(CursorKind::FleurMove) == "fleur-move" &&
              cursor_kind_name(CursorKind::BottomRightResize) ==
                  "bottom-right-resize" &&
              cursor_kind_name(CursorKind::HorizontalResize) ==
                  "horizontal-resize" &&
              cursor_kind_name(CursorKind::VerticalResize) ==
                  "vertical-resize" &&
              cursor_kind_name(CursorKind::Watch) == "watch" &&
              cursor_kind_name(CursorKind::HiddenGlyph) == "hidden-glyph",
          "cursor diagnostics use stable professional kind labels");
  for (const auto [code, kind] : builtins) {
    auto builtin = make_glyph_cursor(glyph(code), error);
    require(builtin && builtin->kind == kind && builtin->width == 16 &&
                builtin->height == 16 &&
                builtin->source_character == code &&
                builtin->mask_character == code + 1U &&
                std::ranges::any_of(builtin->premultiplied_argb,
                                    [](const auto pixel) { return pixel != 0; }),
            "installed cursorfont.h glyph pair has a bounded built-in image");
  }
  auto unsupported = glyph(100);
  require(!make_glyph_cursor(unsupported, error) &&
              error.find("supported M11") != std::string::npos,
          "unclaimed cursor-font glyph is rejected");

  CursorGlyphSpec hidden{CursorFontIdentity::Nil2, CursorFontIdentity::Fixed,
                         static_cast<std::uint16_t>('X'),
                         static_cast<std::uint16_t>(' '), {}, {0xffff, 0xffff, 0xffff}};
  auto hidden_image = make_glyph_cursor(hidden, error);
  require(hidden_image && hidden_image->kind == CursorKind::HiddenGlyph &&
              hidden_image->source_character == 'X' &&
              hidden_image->mask_character == ' ' &&
              std::ranges::all_of(hidden_image->premultiplied_argb,
                                  [](const auto pixel) { return pixel == 0; }),
          "xterm X/space nil2-fixed glyph path creates a transparent cursor");

  CursorStore store;
  require(store.create(1, 100, image, error) == CursorStoreStatus::Success &&
              store.find(100) == image && store.size() == 1 &&
              store.total_bytes() == image->byte_size(),
          "cursor store records ownership, XID, image, and memory");
  require(store.create(2, 100, hidden_image, error) ==
              CursorStoreStatus::DuplicateId,
          "cursor XIDs are immediately unique");
  auto active_reference = store.find(100);
  require(store.free_cursor(100) == CursorStoreStatus::Success &&
              !store.find(100) && active_reference == image &&
              active_reference.use_count() >= 2,
          "free invalidates XID while active shared image remains alive");

  require(store.create(1, 101, image, error) == CursorStoreStatus::Success &&
              store.create(1, 102, hidden_image, error) == CursorStoreStatus::Success &&
              store.create(2, 103, image, error) == CursorStoreStatus::Success &&
              store.cleanup_owner(1) == 2 && store.find(103) && store.size() == 1,
          "client cleanup removes every cursor owned by that client only");

  auto old_color = store.find(103);
  require(store.recolor(103, {0, 0xffff, 0}, {0, 0, 0xffff}, error) ==
              CursorStoreStatus::Success &&
              store.find(103) != old_color &&
              store.find(103)->premultiplied_argb[0] == 0xff00ff00 &&
              old_color->premultiplied_argb[0] == 0xffff0000,
          "recolor swaps a rebuilt image atomically and preserves old references");

  CursorLimits one_cursor;
  one_cursor.maximum_cursors_per_client = 1;
  CursorStore limited(one_cursor);
  require(limited.create(7, 1, image, error) == CursorStoreStatus::Success &&
              limited.create(7, 2, image, error) ==
                  CursorStoreStatus::LimitExceeded,
          "per-client cursor count is bounded");
  CursorLimits tiny_memory;
  tiny_memory.maximum_total_bytes = image->byte_size() - 1;
  CursorStore memory_limited(tiny_memory);
  require(memory_limited.create(1, 1, image, error) ==
              CursorStoreStatus::LimitExceeded,
          "total cursor bytes are bounded");

  auto root_cursor = make_glyph_cursor(glyph(kCursorGlyphLeftPointer), error);
  auto text_cursor = make_glyph_cursor(glyph(kCursorGlyphXterm), error);
  auto move_cursor = make_glyph_cursor(glyph(kCursorGlyphFleur), error);
  const std::vector<CursorWindowNode> windows{
      {1, 0, nullptr}, {2, 1, text_cursor}, {3, 2, nullptr}, {4, 3, move_cursor}};
  require(effective_window_cursor(windows, 4, 1, root_cursor, error) == move_cursor &&
              effective_window_cursor(windows, 3, 1, root_cursor, error) == text_cursor &&
              effective_window_cursor(windows, 1, 1, root_cursor, error) == root_cursor,
          "effective cursor walks pointer target ancestors and defaults at root");
  require(!effective_window_cursor(windows, 99, 1, root_cursor, error) &&
              !error.empty(),
          "invalid cursor inheritance tree is reported");

  CursorSurfacePublication publication;
  require(make_cursor_publication(1, 2, 3, image, 0, 0, true, publication,
                                  error) &&
              publication.x == -1 && publication.y == -1 &&
              publication.image == image && publication.x11_window_id == 0 &&
              publication.parent_surface_id == 0 &&
              publication.cursor_presentation &&
              publication.format ==
                  CursorSurfaceFormat::Argb8888Premultiplied &&
              publication.scale_numerator == 1 &&
              publication.scale_denominator == 1 &&
              publication.transform == CursorSurfaceTransform::Normal &&
              publication.opacity == 1.0F,
          "cursor publication freezes authority and presentation invariants");
  auto clip = clip_cursor(publication, 3, 3);
  require(clip.source_x == 1 && clip.source_y == 1 &&
              clip.destination_x == 0 && clip.destination_y == 0 &&
              clip.width == 1 && clip.height == 1,
          "negative hotspot placement clips source and destination exactly");
  std::vector<std::uint32_t> frame(9, 0xff112233);
  require(composite_cursor(frame, 3, 3, publication, error) &&
              frame[0] == 0xffff0000 &&
              std::ranges::count(frame, 0xff112233) == 8,
          "headless cursor raster clips and composites after the frame");
  publication.visible = false;
  const auto unchanged = frame;
  require(composite_cursor(frame, 3, 3, publication, error) && frame == unchanged,
          "hidden cursor publication leaves the frame unchanged");
  require(!composite_cursor(std::span<std::uint32_t>(frame).first(8), 3, 3,
                            publication, error) && !error.empty(),
          "headless framebuffer dimensions are validated");

  require(make_cursor_publication(1, 2, 3, hidden_image, 10, 10, true,
                                  publication, error),
          "transparent hidden cursor remains a valid publication");
  std::vector<std::uint32_t> hidden_frame(400, 0xffabcdef);
  require(composite_cursor(hidden_frame, 20, 20, publication, error) &&
              std::ranges::all_of(hidden_frame,
                                  [](const auto pixel) { return pixel == 0xffabcdef; }),
          "hidden glyph cursor is semantically invisible when composited");
}
