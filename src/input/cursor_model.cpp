#include "input/cursor_model.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace glasswyrm::input {
namespace {

constexpr std::uint16_t kBuiltinExtent = 16;

std::uint8_t color8(const std::uint16_t value) noexcept {
  return static_cast<std::uint8_t>(value >> 8U);
}

std::uint32_t opaque_argb(const CursorColor color) noexcept {
  return 0xff000000U | (static_cast<std::uint32_t>(color8(color.red)) << 16U) |
         (static_cast<std::uint32_t>(color8(color.green)) << 8U) |
         color8(color.blue);
}

bool valid_bits(const std::span<const std::uint8_t> bits) noexcept {
  return std::ranges::all_of(bits, [](const std::uint8_t bit) { return bit <= 1; });
}

std::shared_ptr<CursorImage> build_cursor(const CursorPixmapSpec& spec,
                                          const CursorKind kind,
                                          std::string& error) {
  error.clear();
  const auto pixels = static_cast<std::size_t>(spec.width) * spec.height;
  if (spec.width == 0 || spec.height == 0 || spec.width > 64 ||
      spec.height > 64) {
    error = "cursor dimensions must be between 1x1 and 64x64";
    return nullptr;
  }
  if (spec.hotspot_x >= spec.width || spec.hotspot_y >= spec.height) {
    error = "cursor hotspot is outside the cursor image";
    return nullptr;
  }
  if (spec.source_bits.size() != pixels || spec.mask_bits.size() != pixels ||
      !valid_bits(spec.source_bits) || !valid_bits(spec.mask_bits)) {
    error = "cursor source and mask must contain one binary value per pixel";
    return nullptr;
  }

  auto image = std::make_shared<CursorImage>();
  image->kind = kind;
  image->source_pixmap = spec.source_pixmap;
  image->mask_pixmap = spec.mask_pixmap;
  image->width = spec.width;
  image->height = spec.height;
  image->hotspot_x = spec.hotspot_x;
  image->hotspot_y = spec.hotspot_y;
  image->foreground = spec.foreground;
  image->background = spec.background;
  image->source_bits.assign(spec.source_bits.begin(), spec.source_bits.end());
  image->mask_bits.assign(spec.mask_bits.begin(), spec.mask_bits.end());
  image->premultiplied_argb.resize(pixels);
  const auto foreground = opaque_argb(spec.foreground);
  const auto background = opaque_argb(spec.background);
  for (std::size_t index = 0; index < pixels; ++index) {
    image->premultiplied_argb[index] =
        spec.mask_bits[index] == 0
            ? 0
            : (spec.source_bits[index] != 0 ? foreground : background);
  }
  return image;
}

void set_pixel(std::vector<std::uint8_t>& bits, const int x, const int y,
               const std::uint8_t value = 1) {
  if (x < 0 || y < 0 || x >= kBuiltinExtent || y >= kBuiltinExtent) return;
  bits[(static_cast<std::size_t>(y) * kBuiltinExtent) +
       static_cast<std::size_t>(x)] = value;
}

void expand_mask(const std::vector<std::uint8_t>& source,
                 std::vector<std::uint8_t>& mask) {
  for (int y = 0; y < kBuiltinExtent; ++y) {
    for (int x = 0; x < kBuiltinExtent; ++x) {
      if (source[(static_cast<std::size_t>(y) * kBuiltinExtent) +
                 static_cast<std::size_t>(x)] == 0)
        continue;
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) set_pixel(mask, x + dx, y + dy);
    }
  }
}

void draw_left_pointer(std::vector<std::uint8_t>& source) {
  for (int y = 0; y < 13; ++y)
    for (int x = 0; x <= y / 2; ++x) set_pixel(source, x, y);
  for (int index = 0; index < 6; ++index) set_pixel(source, 4 + index, 9 + index);
}

void draw_xterm(std::vector<std::uint8_t>& source) {
  for (int x = 4; x <= 11; ++x) {
    set_pixel(source, x, 2);
    set_pixel(source, x, 13);
  }
  for (int y = 2; y <= 13; ++y) {
    set_pixel(source, 7, y);
    set_pixel(source, 8, y);
  }
}

void draw_fleur(std::vector<std::uint8_t>& source) {
  for (int index = 2; index <= 13; ++index) {
    set_pixel(source, 7, index);
    set_pixel(source, 8, index);
    set_pixel(source, index, 7);
    set_pixel(source, index, 8);
  }
  for (int offset = 0; offset < 4; ++offset) {
    set_pixel(source, 7 - offset, 2 + offset);
    set_pixel(source, 8 + offset, 2 + offset);
    set_pixel(source, 7 - offset, 13 - offset);
    set_pixel(source, 8 + offset, 13 - offset);
    set_pixel(source, 2 + offset, 7 - offset);
    set_pixel(source, 2 + offset, 8 + offset);
    set_pixel(source, 13 - offset, 7 - offset);
    set_pixel(source, 13 - offset, 8 + offset);
  }
}

void draw_bottom_right(std::vector<std::uint8_t>& source) {
  for (int offset = 0; offset < 11; ++offset) {
    set_pixel(source, 4 + offset, 14 - offset);
    if (offset < 8) set_pixel(source, 5 + offset, 14 - offset);
  }
  for (int edge = 8; edge <= 14; ++edge) {
    set_pixel(source, edge, 14);
    set_pixel(source, 14, edge);
  }
}

void draw_vertical_resize(std::vector<std::uint8_t>& source) {
  for (int y = 2; y <= 13; ++y) {
    set_pixel(source, 7, y);
    set_pixel(source, 8, y);
  }
  for (int offset = 0; offset < 5; ++offset) {
    set_pixel(source, 7 - offset, 2 + offset);
    set_pixel(source, 8 + offset, 2 + offset);
    set_pixel(source, 7 - offset, 13 - offset);
    set_pixel(source, 8 + offset, 13 - offset);
  }
}

void draw_horizontal_resize(std::vector<std::uint8_t>& source) {
  for (int x = 2; x <= 13; ++x) {
    set_pixel(source, x, 7);
    set_pixel(source, x, 8);
  }
  for (int offset = 0; offset < 5; ++offset) {
    set_pixel(source, 2 + offset, 7 - offset);
    set_pixel(source, 2 + offset, 8 + offset);
    set_pixel(source, 13 - offset, 7 - offset);
    set_pixel(source, 13 - offset, 8 + offset);
  }
}

void draw_watch(std::vector<std::uint8_t>& source) {
  for (int x = 5; x <= 10; ++x) {
    set_pixel(source, x, 3);
    set_pixel(source, x, 12);
  }
  for (int y = 5; y <= 10; ++y) {
    set_pixel(source, 3, y);
    set_pixel(source, 12, y);
  }
  for (int value : {4, 11}) {
    set_pixel(source, value, 4);
    set_pixel(source, value, 11);
  }
  for (int y = 5; y <= 8; ++y) set_pixel(source, 7, y);
  for (int x = 7; x <= 10; ++x) set_pixel(source, x, 8);
}

bool builtin_kind(const std::uint16_t source_character,
                  CursorKind& kind) noexcept {
  switch (source_character) {
    case kCursorGlyphLeftPointer: kind = CursorKind::LeftPointer; return true;
    case kCursorGlyphXterm: kind = CursorKind::XtermText; return true;
    case kCursorGlyphFleur: kind = CursorKind::FleurMove; return true;
    case kCursorGlyphBottomRightCorner:
      kind = CursorKind::BottomRightResize;
      return true;
    case kCursorGlyphHorizontalDoubleArrow:
      kind = CursorKind::HorizontalResize;
      return true;
    case kCursorGlyphScrollLeft:
    case kCursorGlyphScrollRight:
      kind = CursorKind::HorizontalResize;
      return true;
    case kCursorGlyphScrollDown:
    case kCursorGlyphScrollUp:
      kind = CursorKind::VerticalResize;
      return true;
    case kCursorGlyphVerticalDoubleArrow:
      kind = CursorKind::VerticalResize;
      return true;
    case kCursorGlyphWatch: kind = CursorKind::Watch; return true;
    default: return false;
  }
}

void draw_builtin(const CursorKind kind, std::vector<std::uint8_t>& source) {
  switch (kind) {
    case CursorKind::LeftPointer: draw_left_pointer(source); break;
    case CursorKind::XtermText: draw_xterm(source); break;
    case CursorKind::FleurMove: draw_fleur(source); break;
    case CursorKind::BottomRightResize: draw_bottom_right(source); break;
    case CursorKind::HorizontalResize: draw_horizontal_resize(source); break;
    case CursorKind::VerticalResize: draw_vertical_resize(source); break;
    case CursorKind::Watch: draw_watch(source); break;
    default: break;
  }
}

std::pair<std::uint16_t, std::uint16_t> builtin_hotspot(
    const CursorKind kind) noexcept {
  switch (kind) {
    case CursorKind::LeftPointer: return {0, 0};
    case CursorKind::BottomRightResize: return {14, 14};
    default: return {7, 7};
  }
}

std::uint32_t blend_premultiplied(const std::uint32_t source,
                                  const std::uint32_t destination) noexcept {
  const auto alpha = (source >> 24U) & 0xffU;
  if (alpha == 0) return destination;
  if (alpha == 0xffU) return source;
  const auto inverse = 0xffU - alpha;
  const auto blend = [inverse](const std::uint32_t source_component,
                               const std::uint32_t destination_component) {
    return source_component + ((destination_component * inverse + 127U) / 255U);
  };
  const auto red = blend((source >> 16U) & 0xffU,
                         (destination >> 16U) & 0xffU);
  const auto green = blend((source >> 8U) & 0xffU,
                           (destination >> 8U) & 0xffU);
  const auto blue = blend(source & 0xffU, destination & 0xffU);
  return 0xff000000U | (red << 16U) | (green << 8U) | blue;
}

}  // namespace

std::size_t CursorImage::byte_size() const noexcept {
  return source_bits.size() + mask_bits.size() +
         (premultiplied_argb.size() * sizeof(std::uint32_t));
}

std::shared_ptr<const CursorImage> make_pixmap_cursor(
    const CursorPixmapSpec& spec, std::string& error) {
  return build_cursor(spec, CursorKind::Pixmap, error);
}

std::shared_ptr<const CursorImage> make_glyph_cursor(
    const CursorGlyphSpec& spec, std::string& error) {
  if ((spec.source_font == CursorFontIdentity::Fixed ||
       spec.source_font == CursorFontIdentity::Nil2) &&
      (spec.mask_font == CursorFontIdentity::Fixed ||
       spec.mask_font == CursorFontIdentity::Nil2) &&
      spec.source_character == static_cast<std::uint16_t>('X') &&
      spec.mask_character == static_cast<std::uint16_t>(' ')) {
    constexpr std::uint16_t width = 6, height = 13;
    std::vector<std::uint8_t> source(width * height, 0);
    std::vector<std::uint8_t> mask(width * height, 0);
    for (std::uint16_t y = 0; y < height; ++y) {
      source[(static_cast<std::size_t>(y) * width) + (y % width)] = 1;
      source[(static_cast<std::size_t>(y) * width) +
             (width - 1U - (y % width))] = 1;
    }
    CursorPixmapSpec pixmap{0, 0, width, height, 0, 0, source, mask,
                            spec.foreground, spec.background};
    auto result = build_cursor(pixmap, CursorKind::HiddenGlyph, error);
    if (result) {
      result->source_font = spec.source_font;
      result->mask_font = spec.mask_font;
      result->source_character = spec.source_character;
      result->mask_character = spec.mask_character;
    }
    return result;
  }

  CursorKind kind{};
  if (spec.source_font != CursorFontIdentity::Cursor ||
      spec.mask_font != CursorFontIdentity::Cursor ||
      spec.mask_character != spec.source_character + 1U ||
      !builtin_kind(spec.source_character, kind)) {
    error = "glyph cursor is outside the supported M11 cursor-font subset";
    return nullptr;
  }
  std::vector<std::uint8_t> source(kBuiltinExtent * kBuiltinExtent, 0);
  std::vector<std::uint8_t> mask(kBuiltinExtent * kBuiltinExtent, 0);
  draw_builtin(kind, source);
  expand_mask(source, mask);
  const auto [hotspot_x, hotspot_y] = builtin_hotspot(kind);
  CursorPixmapSpec pixmap{0, 0, kBuiltinExtent, kBuiltinExtent, hotspot_x,
                          hotspot_y, source, mask, spec.foreground,
                          spec.background};
  auto result = build_cursor(pixmap, kind, error);
  if (result) {
    result->source_font = spec.source_font;
    result->mask_font = spec.mask_font;
    result->source_character = spec.source_character;
    result->mask_character = spec.mask_character;
  }
  return result;
}

std::shared_ptr<const CursorImage> recolor_cursor(
    const CursorImage& image, const CursorColor foreground,
    const CursorColor background, std::string& error) {
  CursorPixmapSpec spec{image.source_pixmap, image.mask_pixmap,
                        image.width, image.height, image.hotspot_x,
                        image.hotspot_y, image.source_bits, image.mask_bits,
                        foreground, background};
  auto result = build_cursor(spec, image.kind, error);
  if (result) {
    result->source_font = image.source_font;
    result->mask_font = image.mask_font;
    result->source_character = image.source_character;
    result->mask_character = image.mask_character;
  }
  return result;
}

CursorStoreStatus CursorStore::create(
    const std::uint64_t owner, const std::uint32_t xid,
    std::shared_ptr<const CursorImage> image, std::string& error) {
  error.clear();
  if (xid == 0 || !image) {
    error = "cursor resource requires a nonzero XID and a valid image";
    return CursorStoreStatus::InvalidValue;
  }
  if (cursors_.contains(xid)) {
    error = "cursor XID is already allocated";
    return CursorStoreStatus::DuplicateId;
  }
  const auto owner_count = std::ranges::count_if(
      cursors_, [owner](const auto& entry) { return entry.second.owner == owner; });
  if (static_cast<std::size_t>(owner_count) >=
      limits_.maximum_cursors_per_client) {
    error = "client cursor limit exceeded";
    return CursorStoreStatus::LimitExceeded;
  }
  if (image->width > limits_.maximum_extent ||
      image->height > limits_.maximum_extent ||
      image->byte_size() > limits_.maximum_total_bytes - total_bytes_) {
    error = "cursor memory or extent limit exceeded";
    return CursorStoreStatus::LimitExceeded;
  }
  total_bytes_ += image->byte_size();
  cursors_.emplace(xid, Record{owner, std::move(image)});
  return CursorStoreStatus::Success;
}

CursorStoreStatus CursorStore::recolor(const std::uint32_t xid,
                                       const CursorColor foreground,
                                       const CursorColor background,
                                       std::string& error) {
  const auto cursor = cursors_.find(xid);
  if (cursor == cursors_.end()) {
    error = "cursor XID does not exist";
    return CursorStoreStatus::NotFound;
  }
  auto replacement = recolor_cursor(*cursor->second.image, foreground,
                                    background, error);
  if (!replacement) return CursorStoreStatus::InvalidValue;
  const auto old_bytes = cursor->second.image->byte_size();
  const auto new_bytes = replacement->byte_size();
  if (new_bytes > limits_.maximum_total_bytes - (total_bytes_ - old_bytes)) {
    error = "recolored cursor exceeds the cursor memory limit";
    return CursorStoreStatus::LimitExceeded;
  }
  total_bytes_ = total_bytes_ - old_bytes + new_bytes;
  cursor->second.image = std::move(replacement);
  return CursorStoreStatus::Success;
}

CursorStoreStatus CursorStore::free_cursor(const std::uint32_t xid) noexcept {
  const auto cursor = cursors_.find(xid);
  if (cursor == cursors_.end()) return CursorStoreStatus::NotFound;
  total_bytes_ -= cursor->second.image->byte_size();
  cursors_.erase(cursor);
  return CursorStoreStatus::Success;
}

std::size_t CursorStore::cleanup_owner(const std::uint64_t owner) noexcept {
  std::size_t removed = 0;
  for (auto cursor = cursors_.begin(); cursor != cursors_.end();) {
    if (cursor->second.owner != owner) {
      ++cursor;
      continue;
    }
    total_bytes_ -= cursor->second.image->byte_size();
    cursor = cursors_.erase(cursor);
    ++removed;
  }
  return removed;
}

std::shared_ptr<const CursorImage> CursorStore::find(
    const std::uint32_t xid) const noexcept {
  const auto cursor = cursors_.find(xid);
  return cursor == cursors_.end() ? nullptr : cursor->second.image;
}

std::shared_ptr<const CursorImage> effective_window_cursor(
    const std::span<const CursorWindowNode> windows,
    const std::uint32_t pointer_target, const std::uint32_t root,
    std::shared_ptr<const CursorImage> root_default, std::string& error) {
  error.clear();
  auto current = pointer_target;
  for (std::size_t depth = 0; depth <= windows.size(); ++depth) {
    const auto window = std::ranges::find_if(
        windows, [current](const CursorWindowNode& item) {
          return item.xid == current;
        });
    if (window == windows.end()) {
      error = "cursor inheritance encountered an unknown window";
      return nullptr;
    }
    if (window->cursor) return window->cursor;
    if (current == root) {
      if (!root_default) error = "root cursor default is unavailable";
      return root_default;
    }
    current = window->parent;
  }
  error = "cursor inheritance encountered a window-parent cycle";
  return nullptr;
}

bool make_cursor_publication(
    const std::uint64_t surface_id, const std::uint64_t buffer_id,
    const std::uint64_t output_id, std::shared_ptr<const CursorImage> image,
    const std::int32_t pointer_x, const std::int32_t pointer_y,
    const bool visible, CursorSurfacePublication& publication,
    std::string& error) {
  error.clear();
  if (surface_id == 0 || buffer_id == 0 || output_id == 0 || !image) {
    error = "cursor publication requires nonzero IDs and a cursor image";
    return false;
  }
  if (image->width > 64 || image->height > 64) {
    error = "cursor publication exceeds the 64x64 surface limit";
    return false;
  }
  const auto x = static_cast<std::int64_t>(pointer_x) - image->hotspot_x;
  const auto y = static_cast<std::int64_t>(pointer_y) - image->hotspot_y;
  if (x < std::numeric_limits<std::int32_t>::min() ||
      x > std::numeric_limits<std::int32_t>::max() ||
      y < std::numeric_limits<std::int32_t>::min() ||
      y > std::numeric_limits<std::int32_t>::max()) {
    error = "cursor hotspot placement exceeds signed surface coordinates";
    return false;
  }
  CursorSurfacePublication next;
  next.surface_id = surface_id;
  next.buffer_id = buffer_id;
  next.output_id = output_id;
  next.visible = visible;
  next.x = static_cast<std::int32_t>(x);
  next.y = static_cast<std::int32_t>(y);
  next.image = std::move(image);
  publication = std::move(next);
  return true;
}

CursorClip clip_cursor(const CursorSurfacePublication& publication,
                       const std::uint32_t output_width,
                       const std::uint32_t output_height) noexcept {
  CursorClip clip;
  if (!publication.visible || !publication.image || output_width == 0 ||
      output_height == 0)
    return clip;
  const auto left = std::max<std::int64_t>(0, publication.x);
  const auto top = std::max<std::int64_t>(0, publication.y);
  const auto right = std::min<std::int64_t>(
      output_width,
      static_cast<std::int64_t>(publication.x) + publication.image->width);
  const auto bottom = std::min<std::int64_t>(
      output_height,
      static_cast<std::int64_t>(publication.y) + publication.image->height);
  if (left >= right || top >= bottom) return clip;
  clip.source_x = static_cast<std::uint16_t>(left - publication.x);
  clip.source_y = static_cast<std::uint16_t>(top - publication.y);
  clip.destination_x = static_cast<std::uint32_t>(left);
  clip.destination_y = static_cast<std::uint32_t>(top);
  clip.width = static_cast<std::uint16_t>(right - left);
  clip.height = static_cast<std::uint16_t>(bottom - top);
  return clip;
}

bool composite_cursor(const std::span<std::uint32_t> framebuffer,
                      const std::uint32_t frame_width,
                      const std::uint32_t frame_height,
                      const CursorSurfacePublication& publication,
                      std::string& error) {
  error.clear();
  const auto required = static_cast<std::uint64_t>(frame_width) * frame_height;
  if (required != framebuffer.size()) {
    error = "cursor framebuffer size does not match its dimensions";
    return false;
  }
  const auto clip = clip_cursor(publication, frame_width, frame_height);
  if (clip.empty()) return true;
  for (std::uint16_t y = 0; y < clip.height; ++y) {
    for (std::uint16_t x = 0; x < clip.width; ++x) {
      const auto source_index =
          (static_cast<std::size_t>(clip.source_y + y) *
           publication.image->width) + clip.source_x + x;
      const auto destination_index =
          (static_cast<std::size_t>(clip.destination_y + y) * frame_width) +
          clip.destination_x + x;
      framebuffer[destination_index] = blend_premultiplied(
          publication.image->premultiplied_argb[source_index],
          framebuffer[destination_index]);
    }
  }
  return true;
}

}  // namespace glasswyrm::input
