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
      const auto child_outer_x = origin_x + child->x;
      const auto child_outer_y = origin_y + child->y;
      const auto border = static_cast<std::int64_t>(child->border_width);
      const auto child_x = child_outer_x + border;
      const auto child_y = child_outer_y + border;
      const auto right = child_outer_x + child->width + border * 2;
      const auto bottom = child_outer_y + child->height + border * 2;
      if (x >= child_outer_x && y >= child_outer_y && x < right && y < bottom) {
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

std::optional<std::pair<std::int64_t, std::int64_t>> window_root_origin(
    const server::ResourceTable& resources,
    const std::uint32_t window) noexcept {
  const auto root = resources.screen().root_window;
  if (window == root) return std::pair<std::int64_t, std::int64_t>{0, 0};
  auto current = window;
  std::int64_t x = 0;
  std::int64_t y = 0;
  const auto maximum_depth =
      resources.resource_count(server::ResourceType::Window);
  for (std::size_t depth = 0; depth < maximum_depth; ++depth) {
    const auto* candidate = resources.find_window(current);
    if (!candidate || candidate->parent == 0 || candidate->parent == current)
      return std::nullopt;
    x += candidate->x;
    y += candidate->y;
    if (candidate->parent != root) {
      x += candidate->border_width;
      y += candidate->border_width;
    }
    if (candidate->parent == root) return std::pair{x, y};
    current = candidate->parent;
  }
  return std::nullopt;
}

std::uint32_t immediate_child_on_path(
    const server::ResourceTable& resources, const std::uint32_t ancestor,
    const std::uint32_t descendant) noexcept {
  if (ancestor == descendant) return 0;
  auto current = descendant;
  const auto maximum_depth =
      resources.resource_count(server::ResourceType::Window);
  for (std::size_t depth = 0; depth < maximum_depth; ++depth) {
    const auto* candidate = resources.find_window(current);
    if (!candidate || candidate->parent == 0 || candidate->parent == current)
      return 0;
    if (candidate->parent == ancestor) return current;
    current = candidate->parent;
  }
  return 0;
}

std::int16_t clamp_event_coordinate(const std::int64_t value) noexcept {
  return static_cast<std::int16_t>(std::clamp<std::int64_t>(
      value, std::numeric_limits<std::int16_t>::min(),
      std::numeric_limits<std::int16_t>::max()));
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
  EventCoordinates result{clamp_event_coordinate(root_x),
                          clamp_event_coordinate(root_y),
                          clamp_event_coordinate(root_x),
                          clamp_event_coordinate(root_y),
                          immediate_child_on_path(resources, event_window,
                                                  pointer_target)};
  const auto origin = window_root_origin(resources, event_window);
  if (origin) {
    result.event_x = clamp_event_coordinate(
        static_cast<std::int64_t>(root_x) - origin->first);
    result.event_y = clamp_event_coordinate(
        static_cast<std::int64_t>(root_y) - origin->second);
  }
  return result;
}

std::pair<gw::protocol::x11::NotifyDetail, gw::protocol::x11::NotifyDetail>
crossing_details(const server::ResourceTable& resources,
                 const std::uint32_t old_target,
                 const std::uint32_t new_target) noexcept {
  const auto is_ancestor = [&](const std::uint32_t ancestor,
                               std::uint32_t descendant) {
    const auto maximum_depth =
        resources.resource_count(server::ResourceType::Window);
    for (std::size_t depth = 0; depth < maximum_depth; ++depth) {
      if (descendant == ancestor) return true;
      const auto* candidate = resources.find_window(descendant);
      if (!candidate || candidate->parent == 0 ||
          candidate->parent == descendant)
        return false;
      descendant = candidate->parent;
    }
    return false;
  };
  using D = gw::protocol::x11::NotifyDetail;
  if (is_ancestor(old_target, new_target)) return {D::Inferior, D::Ancestor};
  if (is_ancestor(new_target, old_target)) return {D::Ancestor, D::Inferior};
  return {D::Nonlinear, D::Nonlinear};
}

std::pair<gw::protocol::x11::NotifyDetail, gw::protocol::x11::NotifyDetail>
crossing_details(const std::span<const std::uint32_t> old_ancestry,
                 const std::span<const std::uint32_t> new_ancestry) noexcept {
  using D = gw::protocol::x11::NotifyDetail;
  if (old_ancestry.empty() || new_ancestry.empty())
    return {D::Nonlinear, D::Nonlinear};
  if (std::ranges::find(new_ancestry, old_ancestry.front()) !=
      new_ancestry.end())
    return {D::Inferior, D::Ancestor};
  if (std::ranges::find(old_ancestry, new_ancestry.front()) !=
      old_ancestry.end())
    return {D::Ancestor, D::Inferior};
  return {D::Nonlinear, D::Nonlinear};
}

bool crossing_focus(const std::uint32_t root, const std::uint32_t event,
                    const std::uint32_t focus) noexcept {
  return event == focus || (event == root && focus != root);
}

}  // namespace glasswyrm::input
