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
};

struct LifecycleSnapshot {
  std::map<std::uint32_t, LifecycleWindow> windows;
  std::vector<std::uint32_t> root_order;
  std::uint32_t focused_window{};
};

struct AppliedPolicyWindow {
  std::uint32_t xid{};
  std::int32_t x{}, y{};
  std::uint32_t width{}, height{};
  std::int32_t stacking{-1};
  bool visible{}, focused{};
};

}  // namespace glasswyrm::server
