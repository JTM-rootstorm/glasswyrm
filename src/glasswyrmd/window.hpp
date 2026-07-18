#pragma once

#include "glasswyrmd/property.hpp"
#include "glasswyrmd/pixel_storage.hpp"

#include <cstdint>
#include <unordered_map>
#include <memory>
#include <optional>
#include <vector>

namespace glasswyrm::input {
struct CursorImage;
}

namespace glasswyrm::server {

enum class WindowClass : std::uint16_t {
  CopyFromParent = 0,
  InputOutput = 1,
  InputOnly = 2,
};

enum class MapState : std::uint8_t {
  Unmapped = 0,
  Unviewable = 1,
  Viewable = 2,
};
enum class LifecycleStackMode : std::uint8_t { None, Above, Below };
enum class BackgroundSource : std::uint8_t { None, ParentRelative, Pixel };
enum class PolicyWindowType : std::uint8_t {
  Unknown = 0,
  Normal = 1,
  Dialog = 2,
  Utility = 3,
};
enum class PolicyDecoration : std::uint8_t { Unknown = 0, False = 1, True = 2 };
enum class WindowScalePresentationState : std::uint8_t {
  Legacy,
  ScaleAwareAwaitingPixmap,
  ScaleAwareActive,
};

struct WindowScaleState {
  std::uint32_t primary_output{};
  std::uint32_t preferred_scale_numerator{1};
  std::uint32_t preferred_scale_denominator{1};
  std::uint32_t accepted_buffer_scale{1};
  std::uint64_t layout_generation{1};
  std::vector<std::uint32_t> output_memberships;
  bool has_output_state{};
  WindowScalePresentationState presentation{
      WindowScalePresentationState::Legacy};
  std::unordered_map<std::uint64_t, std::uint32_t> event_selections;
};

struct SavedWindowGeometry {
  std::int32_t x{}, y{};
  std::uint32_t width{}, height{}, border_width{};
};

struct WindowAttributes {
  BackgroundSource background_source{BackgroundSource::None};
  std::uint32_t background_pixmap{0};
  std::uint32_t background_pixel{0};
  std::uint32_t border_pixmap{0};
  std::uint32_t border_pixel{0};
  std::uint8_t bit_gravity{0};
  std::uint8_t window_gravity{0};
  std::uint8_t backing_store{0};
  std::uint32_t backing_planes{0xffffffff};
  std::uint32_t backing_pixel{0};
  bool override_redirect{false};
  bool save_under{false};
  std::uint32_t do_not_propagate_mask{0};
  std::uint32_t colormap{0};
  std::uint32_t cursor{0};
  bool cursor_inherit{true};
  std::shared_ptr<const input::CursorImage> cursor_image;
};

struct WindowResource {
  std::uint32_t parent{0};
  std::vector<std::uint32_t> children;
  std::int16_t x{0};
  std::int16_t y{0};
  std::uint16_t width{0};
  std::uint16_t height{0};
  std::uint16_t border_width{0};
  std::int32_t requested_x{0};
  std::int32_t requested_y{0};
  std::uint32_t requested_width{0};
  std::uint32_t requested_height{0};
  std::uint32_t requested_border_width{0};
  std::uint8_t depth{0};
  WindowClass window_class{WindowClass::InputOutput};
  std::uint32_t visual{0};
  MapState map_state{MapState::Unmapped};
  bool map_requested{false};
  bool policy_visible{false};
  bool focused{false};
  std::int32_t stacking{-1};
  bool cleanup_pending{false};
  std::uint64_t creation_serial{0};
  std::uint64_t map_serial{0};
  std::uint64_t focus_serial{0};
  std::uint64_t geometry_serial{0};
  std::uint64_t stack_serial{0};
  std::uint32_t stack_sibling{0};
  LifecycleStackMode stack_mode{LifecycleStackMode::None};
  WindowAttributes attributes;
  std::shared_ptr<PixelStorage> storage;
  std::unordered_map<std::uint64_t, std::uint32_t> event_selections;
  std::unordered_map<std::uint32_t, Property> properties;
  std::uint32_t transient_for{};
  PolicyWindowType policy_window_type{PolicyWindowType::Normal};
  PolicyDecoration decoration_preference{PolicyDecoration::Unknown};
  bool fullscreen_requested{};
  bool maximized_requested{};
  bool above_requested{};
  bool bypass_compositor{};
  bool attention_requested{};
  bool input_requested{true};
  std::uint32_t minimum_width{}, minimum_height{};
  std::uint32_t maximum_width{}, maximum_height{};
  std::optional<SavedWindowGeometry> saved_normal_geometry;
  WindowScaleState scale;
};

struct WindowCreateSpec {
  std::uint32_t xid{0};
  std::uint32_t parent{0};
  std::int16_t x{0};
  std::int16_t y{0};
  std::uint16_t width{0};
  std::uint16_t height{0};
  std::uint16_t border_width{0};
  std::uint8_t depth{0};
  WindowClass window_class{WindowClass::CopyFromParent};
  std::uint32_t visual{0};
  std::uint32_t attribute_mask{0};
  std::uint32_t initial_event_mask{0};
  WindowAttributes attributes;
};

}  // namespace glasswyrm::server
