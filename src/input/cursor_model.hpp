#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace glasswyrm::input {

inline constexpr std::uint16_t kCursorGlyphBottomRightCorner = 14;
inline constexpr std::uint16_t kCursorGlyphFleur = 52;
inline constexpr std::uint16_t kCursorGlyphLeftPointer = 68;
inline constexpr std::uint16_t kCursorGlyphWatch = 150;
inline constexpr std::uint16_t kCursorGlyphXterm = 152;

struct CursorColor {
  std::uint16_t red{0};
  std::uint16_t green{0};
  std::uint16_t blue{0};

  [[nodiscard]] bool operator==(const CursorColor&) const noexcept = default;
};

enum class CursorKind {
  Pixmap,
  LeftPointer,
  XtermText,
  FleurMove,
  BottomRightResize,
  Watch,
  HiddenGlyph,
};

[[nodiscard]] constexpr std::string_view
cursor_kind_name(const CursorKind kind) noexcept {
  switch (kind) {
    case CursorKind::Pixmap:
      return "pixmap";
    case CursorKind::LeftPointer:
      return "left-pointer";
    case CursorKind::XtermText:
      return "xterm-text";
    case CursorKind::FleurMove:
      return "fleur-move";
    case CursorKind::BottomRightResize:
      return "bottom-right-resize";
    case CursorKind::Watch:
      return "watch";
    case CursorKind::HiddenGlyph:
      return "hidden-glyph";
  }
  return "unknown";
}

enum class CursorFontIdentity { Cursor, Fixed, Nil2 };

struct CursorPixmapSpec {
  std::uint32_t source_pixmap{0};
  std::uint32_t mask_pixmap{0};
  std::uint16_t width{0};
  std::uint16_t height{0};
  std::uint16_t hotspot_x{0};
  std::uint16_t hotspot_y{0};
  std::span<const std::uint8_t> source_bits;
  std::span<const std::uint8_t> mask_bits;
  CursorColor foreground;
  CursorColor background{0xffff, 0xffff, 0xffff};
};

struct CursorGlyphSpec {
  CursorFontIdentity source_font{CursorFontIdentity::Cursor};
  CursorFontIdentity mask_font{CursorFontIdentity::Cursor};
  std::uint16_t source_character{0};
  std::uint16_t mask_character{0};
  CursorColor foreground;
  CursorColor background{0xffff, 0xffff, 0xffff};
};

struct CursorImage {
  CursorKind kind{CursorKind::Pixmap};
  std::uint32_t source_pixmap{0};
  std::uint32_t mask_pixmap{0};
  CursorFontIdentity source_font{CursorFontIdentity::Cursor};
  CursorFontIdentity mask_font{CursorFontIdentity::Cursor};
  std::uint16_t source_character{0};
  std::uint16_t mask_character{0};
  std::uint16_t width{0};
  std::uint16_t height{0};
  std::uint16_t hotspot_x{0};
  std::uint16_t hotspot_y{0};
  CursorColor foreground;
  CursorColor background;
  std::vector<std::uint8_t> source_bits;
  std::vector<std::uint8_t> mask_bits;
  std::vector<std::uint32_t> premultiplied_argb;

  [[nodiscard]] std::size_t byte_size() const noexcept;
};

[[nodiscard]] std::shared_ptr<const CursorImage> make_pixmap_cursor(
    const CursorPixmapSpec& spec, std::string& error);
[[nodiscard]] std::shared_ptr<const CursorImage> make_glyph_cursor(
    const CursorGlyphSpec& spec, std::string& error);
[[nodiscard]] std::shared_ptr<const CursorImage> recolor_cursor(
    const CursorImage& image, CursorColor foreground, CursorColor background,
    std::string& error);

struct CursorLimits {
  std::uint16_t maximum_extent{64};
  std::size_t maximum_cursors_per_client{256};
  std::size_t maximum_total_bytes{4U * 1024U * 1024U};
};

enum class CursorStoreStatus {
  Success,
  DuplicateId,
  InvalidValue,
  LimitExceeded,
  NotFound,
};

class CursorStore {
 public:
  explicit CursorStore(CursorLimits limits = {}) noexcept : limits_(limits) {}

  [[nodiscard]] CursorStoreStatus create(
      std::uint64_t owner, std::uint32_t xid,
      std::shared_ptr<const CursorImage> image, std::string& error);
  [[nodiscard]] CursorStoreStatus recolor(std::uint32_t xid,
                                          CursorColor foreground,
                                          CursorColor background,
                                          std::string& error);
  [[nodiscard]] CursorStoreStatus free_cursor(std::uint32_t xid) noexcept;
  [[nodiscard]] std::size_t cleanup_owner(std::uint64_t owner) noexcept;
  [[nodiscard]] std::shared_ptr<const CursorImage> find(
      std::uint32_t xid) const noexcept;
  [[nodiscard]] std::size_t size() const noexcept { return cursors_.size(); }
  [[nodiscard]] std::size_t total_bytes() const noexcept { return total_bytes_; }

 private:
  struct Record {
    std::uint64_t owner{0};
    std::shared_ptr<const CursorImage> image;
  };

  CursorLimits limits_;
  std::unordered_map<std::uint32_t, Record> cursors_;
  std::size_t total_bytes_{0};
};

struct CursorWindowNode {
  std::uint32_t xid{0};
  std::uint32_t parent{0};
  std::shared_ptr<const CursorImage> cursor;
};

[[nodiscard]] std::shared_ptr<const CursorImage> effective_window_cursor(
    std::span<const CursorWindowNode> windows, std::uint32_t pointer_target,
    std::uint32_t root, std::shared_ptr<const CursorImage> root_default,
    std::string& error);

enum class CursorSurfaceFormat { Argb8888Premultiplied };
enum class CursorSurfaceTransform { Normal };

struct CursorSurfacePublication {
  std::uint64_t surface_id{0};
  std::uint64_t buffer_id{0};
  std::uint64_t output_id{0};
  bool cursor_presentation{true};
  std::uint32_t x11_window_id{0};
  std::uint64_t parent_surface_id{0};
  CursorSurfaceFormat format{CursorSurfaceFormat::Argb8888Premultiplied};
  std::uint32_t scale_numerator{1};
  std::uint32_t scale_denominator{1};
  CursorSurfaceTransform transform{CursorSurfaceTransform::Normal};
  float opacity{1.0F};
  bool visible{false};
  std::int32_t x{0};
  std::int32_t y{0};
  std::shared_ptr<const CursorImage> image;
};

[[nodiscard]] bool make_cursor_publication(
    std::uint64_t surface_id, std::uint64_t buffer_id, std::uint64_t output_id,
    std::shared_ptr<const CursorImage> image, std::int32_t pointer_x,
    std::int32_t pointer_y, bool visible, CursorSurfacePublication& publication,
    std::string& error);

struct CursorClip {
  std::uint16_t source_x{0};
  std::uint16_t source_y{0};
  std::uint32_t destination_x{0};
  std::uint32_t destination_y{0};
  std::uint16_t width{0};
  std::uint16_t height{0};

  [[nodiscard]] bool empty() const noexcept { return width == 0 || height == 0; }
};

[[nodiscard]] CursorClip clip_cursor(const CursorSurfacePublication& publication,
                                     std::uint32_t output_width,
                                     std::uint32_t output_height) noexcept;
[[nodiscard]] bool composite_cursor(std::span<std::uint32_t> framebuffer,
                                    std::uint32_t frame_width,
                                    std::uint32_t frame_height,
                                    const CursorSurfacePublication& publication,
                                    std::string& error);

}  // namespace glasswyrm::input
