#pragma once

#include "glasswyrmd/resource_table.hpp"
#include "input/input_state.hpp"
#include "protocol/x11/crossing_event.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace glasswyrm::input {

struct RecipientSelection {
  server::ClientId client{0};
  std::uint32_t mask{0};
  bool live{true};
};
struct RouteWindow {
  std::uint32_t xid{0};
  std::uint32_t parent{0};
  std::uint32_t do_not_propagate{0};
  std::vector<RecipientSelection> selections;
};
struct DeliveryTarget {
  std::uint32_t event_window{0};
  std::vector<server::ClientId> clients;
};
struct EventCoordinates {
  std::int16_t root_x{0}, root_y{0}, event_x{0}, event_y{0};
  std::uint32_t child{0};
};

[[nodiscard]] std::pair<std::int32_t, std::int32_t> clamp_pointer(
    std::int32_t x, std::int32_t y, std::uint32_t width, std::uint32_t height) noexcept;
[[nodiscard]] std::uint32_t hit_test_top_level(const server::ResourceTable& resources,
                                               std::int32_t x, std::int32_t y) noexcept;
[[nodiscard]] std::uint32_t hit_test_deepest_viewable(
    const server::ResourceTable& resources, std::int32_t x,
    std::int32_t y) noexcept;
[[nodiscard]] std::uint32_t managed_top_level_ancestor(
    const server::ResourceTable& resources, std::uint32_t window) noexcept;
[[nodiscard]] std::vector<std::uint32_t> window_ancestry(
    const server::ResourceTable& resources, std::uint32_t window);
[[nodiscard]] std::uint32_t motion_delivery_mask(const InputState& state) noexcept;
[[nodiscard]] DeliveryTarget propagate_event(std::span<const RouteWindow> windows,
                                             std::uint32_t source,
                                             std::uint32_t delivery_mask) noexcept;
[[nodiscard]] DeliveryTarget select_direct(std::span<const RouteWindow> windows,
                                           std::uint32_t window,
                                           std::uint32_t delivery_mask) noexcept;
[[nodiscard]] EventCoordinates event_coordinates(const server::ResourceTable& resources,
                                                 std::uint32_t event_window,
                                                 std::uint32_t pointer_target,
                                                 std::int32_t root_x,
                                                 std::int32_t root_y) noexcept;
[[nodiscard]] std::pair<gw::protocol::x11::NotifyDetail,
                        gw::protocol::x11::NotifyDetail>
crossing_details(std::uint32_t root, std::uint32_t old_target,
                 std::uint32_t new_target) noexcept;
[[nodiscard]] bool crossing_focus(std::uint32_t root, std::uint32_t event,
                                  std::uint32_t focus) noexcept;

}  // namespace glasswyrm::input
