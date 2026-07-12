#pragma once

#include "glasswyrmd/server_state.hpp"
#include "protocol/x11/byte_order.hpp"
#include "protocol/x11/request.hpp"
#include "protocol/x11/lifecycle_request.hpp"
#include "core/geometry/rectangle.hpp"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace glasswyrm::server {

struct DispatchContext {
  ClientId client_id{0};
  std::uint32_t resource_base{0};
  std::uint32_t resource_mask{0};
  std::uint64_t sequence{0};
  gw::protocol::x11::ByteOrder byte_order{
      gw::protocol::x11::ByteOrder::LittleEndian};
  bool integrated_lifecycle{false};
};

enum class StructuralTransitionKind { Map, Unmap, Configure, Destroy };

struct StructuralEventState {
  std::uint32_t target{}, parent{}, above_sibling{};
  std::int16_t x{}, y{};
  std::uint16_t width{}, height{}, border_width{};
  bool override_redirect{}, mapped{}, viewable{};
  std::vector<ClientId> structure_recipients;
  std::vector<ClientId> substructure_recipients;
};

struct StructuralTransition {
  StructuralTransitionKind kind{StructuralTransitionKind::Configure};
  std::optional<StructuralEventState> before;
  std::optional<StructuralEventState> committed;
};

struct DrawableDamage {
  std::uint32_t window{};
  glasswyrm::geometry::Rectangle rectangle{};
};
struct ExposeIntent {
  std::uint32_t window{};
  glasswyrm::geometry::Rectangle rectangle{};
};

enum class DispatchKind { Immediate, DeferredLifecycle, CloseClient };
struct DispatchResult {
  std::vector<std::uint8_t> output;
  DispatchKind kind{DispatchKind::Immediate};
  std::uint32_t deferred_window{0};
  std::optional<gw::protocol::x11::ConfigureWindowRequest> deferred_configure;
  std::optional<WindowCreateSpec> deferred_create;
  bool deferred_destroy{false};
  bool deferred_map{false};
  std::optional<bool> deferred_override_redirect;
  std::vector<StructuralTransition> structural_transitions;
  std::vector<DrawableDamage> drawable_damage;
  std::vector<ExposeIntent> expose_intents;
  DispatchResult() = default;
  DispatchResult(std::vector<std::uint8_t> packet) : output(std::move(packet)) {}
  static DispatchResult deferred(
      std::uint32_t window,
      std::optional<gw::protocol::x11::ConfigureWindowRequest> configure = {},
      bool map = false) {
    DispatchResult result; result.kind = DispatchKind::DeferredLifecycle;
    result.deferred_window = window; result.deferred_configure = std::move(configure);
    result.deferred_map = map;
    return result;
  }
  static DispatchResult deferred_create_window(WindowCreateSpec spec) {
    DispatchResult result;
    result.kind = DispatchKind::DeferredLifecycle;
    result.deferred_window = spec.xid;
    result.deferred_create = std::move(spec);
    return result;
  }
  static DispatchResult deferred_destroy_window(const std::uint32_t window) {
    DispatchResult result;
    result.kind = DispatchKind::DeferredLifecycle;
    result.deferred_window = window;
    result.deferred_destroy = true;
    return result;
  }
  static DispatchResult deferred_override_change(const std::uint32_t window,
                                                 const bool value) {
    DispatchResult result;
    result.kind = DispatchKind::DeferredLifecycle;
    result.deferred_window = window;
    result.deferred_override_redirect = value;
    return result;
  }
};

[[nodiscard]] DispatchResult dispatch_request(
    ServerState& state, const DispatchContext& context,
    const gw::protocol::x11::FramedRequest& request);

}  // namespace glasswyrm::server
