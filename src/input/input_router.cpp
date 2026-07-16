#include "input/input_router.hpp"

#include "protocol/x11/event_mask.hpp"

#include <algorithm>
#include <limits>

namespace glasswyrm::input {
namespace em = gw::protocol::x11::event_mask;

std::pair<std::int32_t, std::int32_t> clamp_pointer(
    const std::int32_t x, const std::int32_t y, const std::uint32_t width,
    const std::uint32_t height) noexcept {
  const auto max_x = width == 0 ? 0 : static_cast<std::int32_t>(std::min<std::uint32_t>(width - 1, std::numeric_limits<std::int32_t>::max()));
  const auto max_y = height == 0 ? 0 : static_cast<std::int32_t>(std::min<std::uint32_t>(height - 1, std::numeric_limits<std::int32_t>::max()));
  return {std::clamp(x, 0, max_x), std::clamp(y, 0, max_y)};
}

std::uint32_t hit_test_top_level(const server::ResourceTable& resources,
                                 const std::int32_t x, const std::int32_t y) noexcept {
  const auto root = resources.screen().root_window;
  const auto* root_window = resources.find_window(root);
  if (!root_window) return root;
  for (auto it = root_window->children.rbegin(); it != root_window->children.rend(); ++it) {
    const auto* w = resources.find_window(*it);
    if (!w || w->parent != root || w->window_class != server::WindowClass::InputOutput ||
        w->map_state != server::MapState::Viewable || !w->policy_visible || w->cleanup_pending)
      continue;
    const auto right = static_cast<std::int64_t>(w->x) + w->width;
    const auto bottom = static_cast<std::int64_t>(w->y) + w->height;
    if (x >= w->x && y >= w->y && x < right && y < bottom) return *it;
  }
  return root;
}

std::uint32_t hit_test_deepest_viewable(
    const server::ResourceTable& resources, const std::int32_t x,
    const std::int32_t y) noexcept {
  const auto root = resources.screen().root_window;
  auto current = hit_test_top_level(resources, x, y);
  if (current == root) return root;
  const auto* window = resources.find_window(current);
  if (!window) return root;
  std::int64_t origin_x = window->x;
  std::int64_t origin_y = window->y;
  const auto maximum_depth =
      resources.resource_count(server::ResourceType::Window);
  for (std::size_t depth = 0; depth < maximum_depth; ++depth) {
    const auto* parent = resources.find_window(current);
    if (!parent) return root;
    std::uint32_t selected = 0;
    std::int64_t selected_x = 0;
    std::int64_t selected_y = 0;
    for (auto child_id = parent->children.rbegin();
         child_id != parent->children.rend(); ++child_id) {
      const auto* child = resources.find_window(*child_id);
      if (!child || child->map_state != server::MapState::Viewable ||
          child->cleanup_pending)
        continue;
      const auto child_x = origin_x + child->x + child->border_width;
      const auto child_y = origin_y + child->y + child->border_width;
      const auto right = child_x + child->width;
      const auto bottom = child_y + child->height;
      if (x >= child_x && y >= child_y && x < right && y < bottom) {
        selected = *child_id;
        selected_x = child_x;
        selected_y = child_y;
        break;
      }
    }
    if (selected == 0) return current;
    current = selected;
    origin_x = selected_x;
    origin_y = selected_y;
  }
  return current;
}

std::uint32_t managed_top_level_ancestor(
    const server::ResourceTable& resources,
    const std::uint32_t window) noexcept {
  const auto root = resources.screen().root_window;
  auto current = window;
  const auto maximum_depth =
      resources.resource_count(server::ResourceType::Window);
  for (std::size_t depth = 0; depth < maximum_depth; ++depth) {
    const auto* candidate = resources.find_window(current);
    if (!candidate) return root;
    if (candidate->parent == root)
      return resources.is_policy_candidate(current) ? current : root;
    if (candidate->parent == 0 || candidate->parent == current) return root;
    current = candidate->parent;
  }
  return root;
}

std::vector<std::uint32_t> window_ancestry(
    const server::ResourceTable& resources, const std::uint32_t window) {
  std::vector<std::uint32_t> result;
  const auto root = resources.screen().root_window;
  auto current = window;
  const auto maximum_depth =
      resources.resource_count(server::ResourceType::Window);
  result.reserve(maximum_depth);
  for (std::size_t depth = 0; depth < maximum_depth; ++depth) {
    const auto* candidate = resources.find_window(current);
    if (!candidate) return {};
    result.push_back(current);
    if (current == root) return result;
    if (candidate->parent == 0 || candidate->parent == current) return {};
    current = candidate->parent;
  }
  return {};
}

std::uint32_t motion_delivery_mask(const InputState& state) noexcept {
  std::uint32_t mask = em::PointerMotion;
  if (state.any_button_down()) mask |= em::ButtonMotion;
  const std::uint32_t per_button[] = {em::Button1Motion, em::Button2Motion,
      em::Button3Motion, em::Button4Motion, em::Button5Motion};
  for (std::uint8_t button = 1; button <= 5; ++button)
    if (state.button_down(button)) mask |= per_button[button - 1];
  return mask;
}

DeliveryTarget propagate_event(const std::span<const RouteWindow> windows,
                               std::uint32_t source,
                               const std::uint32_t delivery_mask) noexcept {
  DeliveryTarget result;
  for (std::size_t depth = 0; depth <= windows.size(); ++depth) {
    const auto found = std::ranges::find(windows, source, &RouteWindow::xid);
    if (found == windows.end()) return result;
    for (const auto& selection : found->selections)
      if (selection.live && (selection.mask & delivery_mask) != 0)
        result.clients.push_back(selection.client);
    if (!result.clients.empty()) { result.event_window = source; return result; }
    if ((found->do_not_propagate & delivery_mask & em::DoNotPropagate) != 0)
      return {};
    if (found->parent == 0 || found->parent == source) return {};
    source = found->parent;
  }
  return {};
}

DeliveryTarget select_direct(const std::span<const RouteWindow> windows,
                             const std::uint32_t window,
                             const std::uint32_t delivery_mask) noexcept {
  DeliveryTarget result{window, {}};
  const auto found = std::ranges::find(windows, window, &RouteWindow::xid);
  if (found == windows.end()) return {};
  for (const auto& selection : found->selections)
    if (selection.live && (selection.mask & delivery_mask) != 0)
      result.clients.push_back(selection.client);
  if (result.clients.empty()) result.event_window = 0;
  return result;
}

EventCoordinates event_coordinates(const server::ResourceTable& resources,
                                   const std::uint32_t event_window,
                                   const std::uint32_t pointer_target,
                                   const std::int32_t root_x,
                                   const std::int32_t root_y) noexcept {
  EventCoordinates result{static_cast<std::int16_t>(root_x), static_cast<std::int16_t>(root_y),
                          static_cast<std::int16_t>(root_x), static_cast<std::int16_t>(root_y), 0};
  if (event_window == resources.screen().root_window) {
    result.child = pointer_target == event_window ? 0 : pointer_target;
  } else if (resources.find_window(event_window)) {
    const auto root = resources.screen().root_window;
    const auto maximum_depth =
        resources.resource_count(server::ResourceType::Window);
    auto current = event_window;
    std::int64_t origin_x = 0;
    std::int64_t origin_y = 0;
    bool resolved = false;
    for (std::size_t depth = 0; depth < maximum_depth; ++depth) {
      const auto* window = resources.find_window(current);
      if (!window || window->parent == 0 || window->parent == current)
        break;
      origin_x += window->x;
      origin_y += window->y;
      if (window->parent != root) {
        origin_x += window->border_width;
        origin_y += window->border_width;
      }
      if (window->parent == root) {
        resolved = true;
        break;
      }
      current = window->parent;
    }
    if (resolved) {
      result.event_x = static_cast<std::int16_t>(root_x - origin_x);
      result.event_y = static_cast<std::int16_t>(root_y - origin_y);
    }
  }
  return result;
}

std::pair<gw::protocol::x11::NotifyDetail, gw::protocol::x11::NotifyDetail>
crossing_details(const std::uint32_t root, const std::uint32_t old_target,
                 const std::uint32_t new_target) noexcept {
  using D = gw::protocol::x11::NotifyDetail;
  if (old_target == root && new_target != root) return {D::Inferior, D::Ancestor};
  if (old_target != root && new_target == root) return {D::Ancestor, D::Inferior};
  return {D::Nonlinear, D::Nonlinear};
}

bool crossing_focus(const std::uint32_t root, const std::uint32_t event,
                    const std::uint32_t focus) noexcept {
  return event == focus || (event == root && focus != root);
}

}  // namespace glasswyrm::input
