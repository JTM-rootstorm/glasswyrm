#pragma once

#include "glasswyrmd/resource_id.hpp"
#include "glasswyrmd/window.hpp"
#include "glasswyrmd/pixmap.hpp"
#include "glasswyrmd/graphics_context.hpp"
#include "glasswyrmd/font.hpp"
#include "glasswyrmd/cursor_resource.hpp"
#include "glasswyrmd/colormap.hpp"
#include "glasswyrmd/shm_segment.hpp"
#include "glasswyrmd/xfixes_region.hpp"
#include "glasswyrmd/damage_resource.hpp"
#include "glasswyrmd/picture.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

namespace glasswyrm::server {

using ClientId = std::uint64_t;

enum class ResourceType {
  Window,
  Pixmap,
  GraphicsContext,
  Font,
  Cursor,
  Colormap,
  ShmSegment,
  XFixesRegion,
  Damage,
  Picture,
};

struct ResourceRecord {
  ResourceType type{ResourceType::Window};
  std::optional<ClientId> owner;
  std::variant<WindowResource, PixmapResource, GraphicsContextResource,
               FontResource, CursorResource, ColormapResource,
               ShmSegmentResource, XFixesRegionResource, DamageResource,
               Picture>
      payload;
};

enum class CreateWindowStatus {
  Success,
  BadIdChoice,
  BadWindow,
  BadValue,
  BadMatch,
  BadAlloc,
};

enum class DestroyWindowStatus { Success, BadWindow, RootPreserved };
enum class LocalLifecycleStatus {
  Success,
  BadWindow,
  BadMatch,
  BadValue,
  BadAlloc,
};
struct LocalConfigure {
  std::optional<std::int32_t> x, y;
  std::optional<std::uint32_t> width, height, border_width;
  std::optional<std::uint32_t> sibling;
  LifecycleStackMode stack_mode{LifecycleStackMode::None};
};
enum class PropertyMutationStatus {
  Success,
  BadWindow,
  BadMatch,
  BadAlloc,
};
enum class PropertyReadStatus { Success, BadWindow, BadValue };
enum class CreatePixmapStatus { Success, BadIdChoice, BadDrawable, BadValue, BadMatch, BadAlloc };
enum class FreePixmapStatus { Success, BadPixmap };
enum class CreateGcStatus { Success, BadIdChoice, BadDrawable, BadMatch, BadAlloc };
enum class FreeGcStatus { Success, BadGContext };
enum class OpenFontStatus { Success, BadIdChoice, BadAlloc };
enum class CloseFontStatus { Success, BadFont };
enum class CreateCursorStatus { Success, BadIdChoice, BadAlloc };
enum class FreeCursorStatus { Success, BadCursor };
enum class RecolorCursorStatus { Success, BadCursor, BadAlloc };
enum class CreateColormapStatus {
  Success,
  BadIdChoice,
  BadWindow,
  BadMatch,
  BadAlloc,
};
enum class FreeColormapStatus { Success, BadColormap, BadAccess };
enum class AttachShmStatus {
  Success,
  BadIdChoice,
  BadValue,
  BadAccess,
  BadAlloc,
};
enum class DetachShmStatus { Success, BadSegment };
enum class RegionStatus {
  Success,
  BadIdChoice,
  BadRegion,
  BadValue,
  BadAlloc,
};
enum class DamageStatus {
  Success,
  BadIdChoice,
  BadDamage,
  BadDrawable,
  BadValue,
  BadAlloc,
};
struct DamageMutationResult {
  DamageStatus status{DamageStatus::Success};
  std::vector<DamageNotification> notifications;
};
enum class PictureResourceStatus {
  Success,
  BadIdChoice,
  BadPicture,
  BadDrawable,
  BadFormat,
  BadMatch,
  BadValue,
  BadAlloc,
};

struct CleanupResult {
  std::size_t resources_destroyed{0};
  std::size_t property_bytes_released{0};
};

struct ClientCleanupWindow {
  std::uint32_t xid{0};
  std::uint32_t parent{0};
  std::vector<ClientId> structure_recipients;
  std::vector<ClientId> substructure_recipients;
  std::optional<ClientId> owner;
};

struct WindowDestroyPlan {
  std::uint32_t root{0};
  std::vector<ClientCleanupWindow> postorder;
};

struct ClientCleanupPlan {
  ClientId owner{0};
  std::vector<std::uint32_t> roots;
  std::vector<ClientCleanupWindow> postorder;
  bool affects_policy{false};
};

struct PropertyReadResult {
  PropertyReadStatus status{PropertyReadStatus::Success};
  bool present{false};
  bool type_matched{false};
  bool deleted{false};
  PropertySlice value;
};

inline constexpr std::size_t kMaximumBytesPerProperty = 4U * 1024U * 1024U;
inline constexpr std::size_t kMaximumTotalPropertyBytes = 64U * 1024U * 1024U;
inline constexpr std::size_t kMaximumPropertiesPerWindow = 4096;

struct ResourceLimits {
  std::size_t maximum_bytes_per_property{kMaximumBytesPerProperty};
  std::size_t maximum_total_property_bytes{kMaximumTotalPropertyBytes};
  std::size_t maximum_properties_per_window{kMaximumPropertiesPerWindow};
  std::size_t maximum_canonical_drawable_bytes{256U * 1024U * 1024U};
  std::size_t maximum_pixmaps{8192};
  std::size_t maximum_graphics_contexts{8192};
  std::size_t maximum_fonts_per_client{256};
  std::size_t maximum_total_fonts{1024};
  std::size_t maximum_cursors_per_client{256};
  std::size_t maximum_total_cursor_bytes{4U * 1024U * 1024U};
  std::size_t maximum_colormaps_per_client{4096};
  std::size_t maximum_shm_segments_per_client{256};
  std::size_t maximum_shm_bytes_per_client{512U * 1024U * 1024U};
  std::size_t maximum_xfixes_regions_per_client{4096};
  std::size_t maximum_xfixes_region_rectangles{
      kMaximumXFixesRegionRectangles};
  std::size_t maximum_damage_resources_per_client{4096};
  std::size_t maximum_pictures_per_client{8192};
};

class ResourceTable {
 public:
  explicit ResourceTable(ScreenModel screen = kScreenModel,
                         ResourceLimits limits = {});

  [[nodiscard]] const ScreenModel& screen() const noexcept { return screen_; }
  [[nodiscard]] bool update_screen_geometry(ScreenModel screen) noexcept;
  [[nodiscard]] const ResourceRecord* find(std::uint32_t xid) const noexcept;
  [[nodiscard]] ResourceRecord* find(std::uint32_t xid) noexcept;
  [[nodiscard]] const WindowResource* find_window(
      std::uint32_t xid) const noexcept;
  [[nodiscard]] WindowResource* find_window(std::uint32_t xid) noexcept;
  [[nodiscard]] const PixmapResource* find_pixmap(std::uint32_t xid) const noexcept;
  [[nodiscard]] PixmapResource* find_pixmap(std::uint32_t xid) noexcept;
  [[nodiscard]] const GraphicsContextResource* find_gc(std::uint32_t xid) const noexcept;
  [[nodiscard]] GraphicsContextResource* find_gc(std::uint32_t xid) noexcept;
  [[nodiscard]] const FontResource* find_font(std::uint32_t xid) const noexcept;
  [[nodiscard]] const CursorResource* find_cursor(
      std::uint32_t xid) const noexcept;
  [[nodiscard]] const ColormapResource* find_colormap(
      std::uint32_t xid) const noexcept;
  [[nodiscard]] bool valid_colormap(std::uint32_t xid) const noexcept;
  [[nodiscard]] const ShmSegmentResource* find_shm_segment(
      std::uint32_t xid) const noexcept;
  [[nodiscard]] ShmSegmentResource* find_shm_segment(
      std::uint32_t xid) noexcept;
  [[nodiscard]] const XFixesRegionResource* find_xfixes_region(
      std::uint32_t xid) const noexcept;
  [[nodiscard]] XFixesRegionResource* find_xfixes_region(
      std::uint32_t xid) noexcept;
  [[nodiscard]] const DamageResource* find_damage(
      std::uint32_t xid) const noexcept;
  [[nodiscard]] DamageResource* find_damage(std::uint32_t xid) noexcept;
  [[nodiscard]] const Picture* find_picture(std::uint32_t xid) const noexcept;
  [[nodiscard]] Picture* find_picture(std::uint32_t xid) noexcept;
  [[nodiscard]] std::shared_ptr<const input::CursorImage> effective_cursor(
      std::uint32_t pointer_target) const noexcept;
  [[nodiscard]] const std::shared_ptr<const input::CursorImage>&
  root_default_cursor() const noexcept { return root_default_cursor_; }
  [[nodiscard]] bool is_policy_candidate(std::uint32_t xid) const noexcept;
  [[nodiscard]] bool create_server_proxy_window(std::uint32_t xid);
  [[nodiscard]] LocalLifecycleStatus set_local_map_intent(std::uint32_t xid,
                                                          bool mapped);
  [[nodiscard]] LocalLifecycleStatus configure_local(
      std::uint32_t xid, const LocalConfigure& configure);
  void recompute_map_states(std::uint32_t xid);
  [[nodiscard]] bool reorder_root_children(
      const std::vector<std::uint32_t>& visible_bottom_to_top);

  [[nodiscard]] bool valid_new_resource_id(std::uint32_t xid,
                                           std::uint32_t resource_base,
                                           std::uint32_t resource_mask) const;
  [[nodiscard]] CreateWindowStatus create_window(
      ClientId owner, std::uint32_t resource_base, std::uint32_t resource_mask,
      const WindowCreateSpec& spec);
  [[nodiscard]] CreatePixmapStatus create_pixmap(
      ClientId owner, std::uint32_t resource_base, std::uint32_t resource_mask,
      std::uint32_t xid, std::uint32_t drawable, std::uint8_t depth,
      std::uint16_t width, std::uint16_t height);
  [[nodiscard]] FreePixmapStatus free_pixmap(std::uint32_t xid);
  [[nodiscard]] CreatePixmapStatus name_window_pixmap(
      ClientId owner, std::uint32_t resource_base, std::uint32_t resource_mask,
      std::uint32_t xid, std::uint32_t window);
  [[nodiscard]] CreateGcStatus create_gc(
      ClientId owner, std::uint32_t resource_base, std::uint32_t resource_mask,
      std::uint32_t xid, std::uint32_t drawable, GraphicsContextResource gc);
  [[nodiscard]] FreeGcStatus free_gc(std::uint32_t xid);
  [[nodiscard]] OpenFontStatus open_font(
      ClientId owner, std::uint32_t resource_base, std::uint32_t resource_mask,
      std::uint32_t xid, FontIdentity identity = FontIdentity::Fixed);
  [[nodiscard]] CloseFontStatus close_font(std::uint32_t xid);
  [[nodiscard]] CreateCursorStatus create_cursor(
      ClientId owner, std::uint32_t resource_base, std::uint32_t resource_mask,
      std::uint32_t xid, std::shared_ptr<const input::CursorImage> image);
  [[nodiscard]] FreeCursorStatus free_cursor(std::uint32_t xid);
  [[nodiscard]] RecolorCursorStatus recolor_cursor(
      std::uint32_t xid, input::CursorColor foreground,
      input::CursorColor background);
  [[nodiscard]] CreateColormapStatus create_colormap(
      ClientId owner, std::uint32_t resource_base, std::uint32_t resource_mask,
      std::uint32_t xid, std::uint32_t window, std::uint32_t visual);
  [[nodiscard]] FreeColormapStatus free_colormap(std::uint32_t xid);
  [[nodiscard]] AttachShmStatus attach_shm_segment(
      ClientId owner, std::uint32_t resource_base, std::uint32_t resource_mask,
      std::uint32_t xid, std::uint32_t shmid, bool read_only,
      std::uint32_t peer_uid);
  [[nodiscard]] DetachShmStatus detach_shm_segment(std::uint32_t xid);
  [[nodiscard]] RegionStatus create_xfixes_region(
      ClientId owner, std::uint32_t resource_base, std::uint32_t resource_mask,
      std::uint32_t xid, std::span<const geometry::Rectangle> rectangles);
  [[nodiscard]] RegionStatus destroy_xfixes_region(std::uint32_t xid);
  [[nodiscard]] RegionStatus set_xfixes_region(
      std::uint32_t xid, std::span<const geometry::Rectangle> rectangles);
  [[nodiscard]] RegionStatus copy_xfixes_region(std::uint32_t source,
                                                std::uint32_t destination);
  [[nodiscard]] RegionStatus combine_xfixes_regions(
      std::uint32_t source1, std::uint32_t source2,
      std::uint32_t destination, std::uint8_t operation);
  [[nodiscard]] RegionStatus translate_xfixes_region(
      std::uint32_t xid, std::int16_t dx, std::int16_t dy);
  [[nodiscard]] RegionStatus extents_xfixes_region(
      std::uint32_t source, std::uint32_t destination);
  [[nodiscard]] DamageStatus create_damage(
      ClientId owner, std::uint32_t resource_base, std::uint32_t resource_mask,
      std::uint32_t xid, std::uint32_t drawable, DamageReportLevel level);
  [[nodiscard]] DamageStatus destroy_damage(std::uint32_t xid);
  [[nodiscard]] DamageMutationResult subtract_damage(
      std::uint32_t xid, std::uint32_t repair_region,
      std::uint32_t parts_region);
  [[nodiscard]] DamageMutationResult add_damage(
      std::uint32_t drawable,
      std::span<const geometry::Rectangle> rectangles);
  [[nodiscard]] std::vector<DamageNotification> damage_drawable(
      std::uint32_t drawable, geometry::Rectangle rectangle);
  [[nodiscard]] PictureResourceStatus create_picture(
      ClientId owner, std::uint32_t resource_base, std::uint32_t resource_mask,
      std::uint32_t xid, Picture picture);
  [[nodiscard]] PictureResourceStatus free_picture(std::uint32_t xid);
  [[nodiscard]] DestroyWindowStatus destroy_window(std::uint32_t xid,
                                                   CleanupResult* result = nullptr);
  [[nodiscard]] std::optional<WindowDestroyPlan> capture_destroy_plan(
      std::uint32_t xid) const;
  [[nodiscard]] DestroyWindowStatus commit_destroy_plan(
      const WindowDestroyPlan& plan, CleanupResult* result = nullptr);
  [[nodiscard]] CleanupResult cleanup_client(ClientId owner);
  [[nodiscard]] ClientCleanupPlan prepare_client_cleanup(ClientId owner);
  [[nodiscard]] CleanupResult commit_client_cleanup(
      const ClientCleanupPlan& plan);
  [[nodiscard]] bool cleanup_pending(std::uint32_t xid) const noexcept;
  [[nodiscard]] bool set_event_selection(std::uint32_t window, ClientId client,
                                         std::uint32_t mask);
  [[nodiscard]] std::uint32_t event_selection(std::uint32_t window,
                                              ClientId client) const noexcept;
  [[nodiscard]] std::uint32_t all_event_selections(
      std::uint32_t window) const noexcept;
  void remove_event_selections(ClientId client) noexcept;

  [[nodiscard]] PropertyMutationStatus change_property(
      std::uint32_t window, std::uint32_t property_atom, Property value,
      PropertyMode mode);
  [[nodiscard]] bool delete_property(std::uint32_t window,
                                     std::uint32_t property_atom);
  [[nodiscard]] PropertyReadResult get_property(
      std::uint32_t window, std::uint32_t property_atom,
      std::uint32_t requested_type, bool delete_after_read,
      std::uint32_t long_offset, std::uint32_t long_length);
  [[nodiscard]] std::vector<std::uint32_t> list_properties(
      std::uint32_t window) const;

  [[nodiscard]] std::size_t resource_count(ResourceType type) const noexcept;
  [[nodiscard]] std::size_t resource_count_by_owner(ClientId owner) const noexcept;
  [[nodiscard]] std::size_t total_property_bytes() const noexcept {
    return total_property_bytes_;
  }
  [[nodiscard]] std::size_t canonical_drawable_bytes() const noexcept {
    return canonical_drawable_bytes_;
  }
  [[nodiscard]] std::size_t total_cursor_bytes() const noexcept {
    return total_cursor_bytes_;
  }
  [[nodiscard]] bool invariants_hold() const noexcept;

 private:
  void destroy_leaf(std::uint32_t xid, CleanupResult& result);
  std::size_t remove_damage_for_drawable(std::uint32_t drawable);
  std::size_t remove_pictures_for_drawable(std::uint32_t drawable);
  void recompute_canonical_drawable_bytes() noexcept;
  void recompute_map_states_from(std::uint32_t xid, bool parent_viewable);

  ScreenModel screen_;
  ResourceLimits limits_;
  std::unordered_map<std::uint32_t, ResourceRecord> resources_;
  std::unordered_map<ClientId, std::vector<std::uint32_t>> resources_by_owner_;
  std::size_t total_property_bytes_{0};
  std::size_t canonical_drawable_bytes_{0};
  std::size_t total_cursor_bytes_{0};
  std::shared_ptr<const input::CursorImage> root_default_cursor_;
};

}  // namespace glasswyrm::server
