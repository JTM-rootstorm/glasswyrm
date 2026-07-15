#pragma once

#include "glasswyrmd/client_connection.hpp"
#include "glasswyrmd/resource_table.hpp"
#include "glasswyrmd/grab_state.hpp"
#include "core/geometry/rectangle.hpp"
#include "input/input_state.hpp"
#include "protocol/x11/input_event.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace glasswyrm::server {

struct DirectInputEventState {
  std::uint32_t window{};
  std::int16_t x{};
  std::int16_t y{};
  std::vector<ClientId> focus_recipients;
  std::vector<ClientId> leave_recipients;
};

struct InputTransitionState {
  std::uint32_t focus{};
  std::uint32_t pointer_target{};
  DirectInputEventState focus_window;
  DirectInputEventState pointer_window;
};

class EventRouter {
public:
  explicit EventRouter(const ResourceTable &resources)
      : resources_(resources) {}

  [[nodiscard]] std::optional<StructuralEventState>
  capture(std::uint32_t target) const;
  [[nodiscard]] std::size_t
  route_transition(StructuralTransitionKind kind,
                   const std::optional<StructuralEventState> &before,
                   const std::optional<StructuralEventState> &committed,
                   std::span<ClientConnection *const> clients) const;

  [[nodiscard]] std::size_t
  route_destroy(std::uint32_t target, std::uint32_t parent,
                std::span<ClientConnection *const> clients) const;
  [[nodiscard]] std::size_t
  route_unmap(std::uint32_t target, std::uint32_t parent,
              std::span<ClientConnection *const> clients) const;
  [[nodiscard]] std::size_t
  route_map(std::uint32_t target, std::uint32_t parent, bool override_redirect,
            std::span<ClientConnection *const> clients) const;
  [[nodiscard]] std::size_t
  route_configure(std::uint32_t target, std::uint32_t parent,
                  std::uint32_t above_sibling, std::int16_t x, std::int16_t y,
                  std::uint16_t width, std::uint16_t height,
                  std::uint16_t border_width, bool override_redirect,
                  std::span<ClientConnection *const> clients) const;
  [[nodiscard]] std::size_t route_expose(
      std::uint32_t window, std::span<const glasswyrm::geometry::Rectangle> rectangles,
      std::span<ClientConnection *const> clients) const;
  [[nodiscard]] std::size_t route_viewable_subtree_expose(
      std::uint32_t window,
      std::span<ClientConnection *const> clients) const;
  [[nodiscard]] std::size_t route_input(
      gw::protocol::x11::CoreEventType type, std::uint8_t detail,
      std::uint32_t time, std::uint32_t source, std::uint16_t state,
      std::uint32_t delivery_mask, std::int32_t root_x, std::int32_t root_y,
      std::uint32_t pointer_target,
      std::span<ClientConnection *const> clients) const;
  [[nodiscard]] std::optional<std::pair<ClientId, std::uint32_t>>
  input_recipient(std::uint32_t source, std::uint32_t delivery_mask) const;
  [[nodiscard]] std::size_t route_input_grabbed(
      const GrabState& grabs, gw::protocol::x11::CoreEventType type,
      std::uint8_t detail, std::uint32_t time, std::uint32_t source,
      std::uint16_t state, std::uint32_t delivery_mask,
      std::int32_t root_x, std::int32_t root_y,
      std::uint32_t pointer_target,
      std::span<ClientConnection *const> clients) const;
  [[nodiscard]] std::size_t route_crossing(
      std::uint32_t old_target, std::uint32_t new_target,
      std::uint32_t focus, const glasswyrm::input::InputState& input,
      std::span<ClientConnection *const> clients) const;
  [[nodiscard]] std::size_t route_focus(
      std::uint32_t old_focus, std::uint32_t new_focus,
      std::span<ClientConnection *const> clients) const;
  [[nodiscard]] InputTransitionState capture_input_transition(
      std::uint32_t focus, std::uint32_t pointer_target) const;
  [[nodiscard]] std::size_t route_lifecycle_input_transition(
      const InputTransitionState& before, std::uint32_t new_focus,
      std::uint32_t new_pointer_target,
      const glasswyrm::input::InputState& input,
      std::span<ClientConnection *const> clients) const;

private:
  const ResourceTable &resources_;
};

} // namespace glasswyrm::server
