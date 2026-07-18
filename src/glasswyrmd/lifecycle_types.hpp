#pragma once

#include "glasswyrmd/window.hpp"

#include <cstdint>
#include <map>
#include <vector>

namespace glasswyrm::server {

struct LifecycleWindow {
  std::uint32_t xid{}, parent{};
  WindowClass window_class{WindowClass::InputOutput};
  std::int32_t requested_x{}, requested_y{};
  std::uint32_t requested_width{}, requested_height{}, requested_border_width{};
  bool override_redirect{}, map_requested{}, policy_visible{}, focused{};
  std::uint64_t creation_serial{}, map_serial{}, focus_serial{}, geometry_serial{};
  std::uint64_t stack_serial{};
  std::uint32_t stack_sibling{};
  LifecycleStackMode stack_mode{LifecycleStackMode::None};
  std::uint32_t transient_for{};
  PolicyWindowType policy_window_type{PolicyWindowType::Normal};
  PolicyDecoration decoration_preference{PolicyDecoration::Unknown};
  bool fullscreen_requested{}, maximized_requested{}, above_requested{};
  bool bypass_compositor{}, attention_requested{}, input_requested{true};
  std::uint32_t minimum_width{}, minimum_height{};
  std::uint32_t maximum_width{}, maximum_height{};
  std::optional<SavedWindowGeometry> saved_normal_geometry;
  std::int32_t applied_x{}, applied_y{}, stacking{-1};
  std::uint32_t applied_width{}, applied_height{};
  std::uint8_t window_type{}, applied_state{};
  bool managed{}, decoration_eligible{};
  std::uint8_t fullscreen_eligible{}, direct_scanout_eligible{};
};

struct LifecycleSnapshot {
  std::map<std::uint32_t, LifecycleWindow> windows;
  std::vector<std::uint32_t> root_order;
  std::uint32_t focused_window{};
  std::uint32_t root_window{1};
  std::uint32_t workspace_id{1};
  std::uint64_t output_id{1};
};

struct AppliedPolicyWindow {
  std::uint32_t xid{};
  std::int32_t x{}, y{};
  std::uint32_t width{}, height{};
  std::int32_t stacking{-1};
  bool visible{}, focused{};
};

}  // namespace glasswyrm::server
