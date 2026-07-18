#pragma once

#include "glasswyrmd/server_state.hpp"
#include "glasswyrmd/keyboard_mapping.hpp"
#include "protocol/x11/byte_order.hpp"
#include "protocol/x11/request.hpp"
#include "protocol/x11/lifecycle_request.hpp"
#include "protocol/x11/event.hpp"
#include "core/geometry/rectangle.hpp"

#include <cstdint>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace glasswyrm::server {

class ExtensionRegistry;

struct InputSnapshot {
  InputSnapshot() = default;
  InputSnapshot(std::int32_t x, std::int32_t y, std::uint16_t mask,
                std::uint32_t target, std::uint32_t time,
                std::array<std::uint8_t, 32> keys = {}) noexcept
      : root_x(x), root_y(y), state_mask(mask), pointer_target(target),
        logical_time(time), keymap(keys) {}

  std::int32_t root_x{0};
  std::int32_t root_y{0};
  std::uint16_t state_mask{0};
  std::uint32_t pointer_target{1};
  std::uint32_t logical_time{1};
  std::array<std::uint8_t, 32> keymap{};
  std::shared_ptr<const KeyboardMappingSnapshot> keyboard_mapping;
  std::function<bool(bool)> set_global_auto_repeat;
  std::function<bool(std::int32_t, std::int32_t)> warp_pointer;
};

struct DispatchContext {
  DispatchContext(ClientId client, std::uint32_t base, std::uint32_t mask,
                  std::uint64_t request_sequence,
                  gw::protocol::x11::ByteOrder order,
                  bool lifecycle = false, InputSnapshot snapshot = {},
                  const ExtensionRegistry* extension_registry = nullptr,
                  std::optional<std::uint32_t> uid = std::nullopt)
      : client_id(client), resource_base(base), resource_mask(mask),
        sequence(request_sequence), byte_order(order),
        integrated_lifecycle(lifecycle), input(std::move(snapshot)),
        extensions(extension_registry), peer_uid(uid) {}

  ClientId client_id{0};
  std::uint32_t resource_base{0};
  std::uint32_t resource_mask{0};
  std::uint64_t sequence{0};
  gw::protocol::x11::ByteOrder byte_order{
      gw::protocol::x11::ByteOrder::LittleEndian};
  bool integrated_lifecycle{false};
  InputSnapshot input{};
  const ExtensionRegistry* extensions{nullptr};
  std::optional<std::uint32_t> peer_uid;
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
  std::optional<glasswyrm::geometry::Rectangle> buffer_rectangle;
};
struct ExposeIntent {
  std::uint32_t window{};
  glasswyrm::geometry::Rectangle rectangle{};
};

struct XFixesSelectionNotifyEvent {
  std::uint8_t subtype{};
  std::uint32_t window{};
  std::uint32_t owner{};
  std::uint32_t selection{};
  std::uint32_t timestamp{};
  std::uint32_t selection_timestamp{};
};

struct DamageNotifyEvent {
  std::uint8_t level{};
  std::uint32_t drawable{};
  std::uint32_t damage{};
  std::uint32_t timestamp{};
  glasswyrm::geometry::Rectangle area{};
  glasswyrm::geometry::Rectangle geometry{};
};

struct RandRScreenChangeNotifyEvent {
  std::uint16_t rotation{};
  std::uint32_t timestamp{};
  std::uint32_t config_timestamp{};
  std::uint32_t root{};
  std::uint32_t request_window{};
  std::uint16_t size_id{};
  std::uint16_t subpixel_order{};
  std::uint16_t width{}, height{}, width_millimeters{}, height_millimeters{};
};

struct RandRCrtcChangeNotifyEvent {
  std::uint32_t timestamp{}, window{}, crtc{}, mode{};
  std::uint16_t rotation{};
  std::int16_t x{}, y{};
  std::uint16_t width{}, height{};
};

struct RandROutputChangeNotifyEvent {
  std::uint32_t timestamp{}, config_timestamp{}, window{}, output{}, crtc{};
  std::uint32_t mode{};
  std::uint16_t rotation{};
  std::uint8_t connection{}, subpixel_order{};
};

struct RandROutputPropertyNotifyEvent {
  std::uint32_t window{}, output{}, atom{}, timestamp{};
  std::uint8_t status{};
};

struct GwScaleNotifyEvent {
  std::uint8_t reason_mask{};
  std::uint32_t window{};
  std::uint32_t primary_output{};
  std::uint32_t preferred_scale_numerator{1};
  std::uint32_t preferred_scale_denominator{1};
  std::uint32_t accepted_buffer_scale{1};
  std::uint64_t layout_generation{1};
};

using ProtocolEvent = std::variant<
    gw::protocol::x11::PropertyNotifyEvent,
    gw::protocol::x11::SelectionClearEvent,
    gw::protocol::x11::SelectionRequestEvent,
    gw::protocol::x11::SelectionNotifyEvent,
    gw::protocol::x11::UnmapNotifyEvent,
    gw::protocol::x11::ClientMessageEvent,
    XFixesSelectionNotifyEvent,
    DamageNotifyEvent,
    RandRScreenChangeNotifyEvent,
    RandRCrtcChangeNotifyEvent,
    RandROutputChangeNotifyEvent,
    RandROutputPropertyNotifyEvent,
    GwScaleNotifyEvent>;

enum class ProtocolEventDelivery { DirectClient, WindowOwner, WindowMask };

struct ProtocolEventIntent {
  ProtocolEventDelivery delivery{ProtocolEventDelivery::DirectClient};
  ClientId client{0};
  std::uint32_t window{0};
  std::uint32_t mask{0};
  bool propagate{false};
  ProtocolEvent event;
};

struct DeferredPropertyMutation {
  std::uint32_t window{};
  std::uint32_t atom{};
  std::optional<Property> value;
  std::uint32_t notify_time{};
};

struct DeferredPolicyMutation {
  LifecycleWindow window;
  std::optional<DeferredPropertyMutation> property;
  bool request_focus{};
};

struct DeferredScaleMutation {
  std::uint32_t window{};
  WindowScaleState scale;
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
  std::optional<DeferredPolicyMutation> deferred_policy;
  std::optional<DeferredScaleMutation> deferred_scale;
  std::vector<StructuralTransition> structural_transitions;
  std::vector<DrawableDamage> drawable_damage;
  std::vector<ExposeIntent> expose_intents;
  std::vector<ProtocolEventIntent> protocol_events;
  bool enable_big_requests{false};
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
  static DispatchResult deferred_policy_change(DeferredPolicyMutation change) {
    DispatchResult result;
    result.kind = DispatchKind::DeferredLifecycle;
    result.deferred_window = change.window.xid;
    result.deferred_policy = std::move(change);
    return result;
  }
  static DispatchResult deferred_scale_change(DeferredScaleMutation change) {
    DispatchResult result;
    result.kind = DispatchKind::DeferredLifecycle;
    result.deferred_window = change.window;
    result.deferred_scale = std::move(change);
    return result;
  }
};

[[nodiscard]] DispatchResult dispatch_request(
    ServerState& state, const DispatchContext& context,
    const gw::protocol::x11::FramedRequest& request);

}  // namespace glasswyrm::server
