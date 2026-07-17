#include "glasswyrmd/resource_table.hpp"
#include "helpers/test_support.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/core.hpp"
#include "protocol/x11/event.hpp"
#include "wm/interactive_policy.hpp"

#include <array>
#include <cstdint>
#include <span>

using namespace glasswyrm::server;
using namespace glasswyrm::wm;
namespace x11 = gw::protocol::x11;
using gw::test::require;

namespace {

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes,
                       const std::size_t offset,
                       const x11::ByteOrder order) {
  x11::ByteReader reader(
      std::span<const std::uint8_t>(bytes).subspan(offset), order);
  std::uint32_t value{};
  require(reader.read_u32(value), "ClientMessage field is present");
  return value;
}

WindowCreateSpec window(const std::uint32_t xid) {
  WindowCreateSpec result;
  result.xid = xid;
  result.parent = 1;
  result.width = 640;
  result.height = 480;
  result.window_class = WindowClass::InputOutput;
  return result;
}

}  // namespace

int main() {
  constexpr std::uint32_t focused = 0x00400001;
  constexpr std::uint32_t sibling = 0x00400002;
  constexpr std::uint32_t wm_protocols = 40;
  constexpr std::uint32_t wm_delete_window = 41;
  constexpr std::uint32_t event_time = 0x10203040;
  const InteractiveBindings bindings;

  const auto supported = evaluate_close_binding(
      bindings, kInteractiveMod1, kInteractiveF4Keysym, focused, true, false,
      true, event_time);
  require(supported.action == CloseAction::SendDeleteWindow &&
              supported.consume_event && supported.target == focused &&
              supported.event_time == event_time,
          "WM_DELETE_WINDOW support selects one protocol ClientMessage");
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian}) {
    const std::array<std::uint32_t, 5> data{wm_delete_window, event_time, 0, 0,
                                            0};
    const auto packet = x11::encode_client_message(
        order, 0x1234,
        {supported.target, wm_protocols, data, false});
    require(packet.size() == 32 &&
                packet[0] ==
                    static_cast<std::uint8_t>(x11::CoreEventType::ClientMessage) &&
                packet[1] == 32 && read_u32(packet, 4, order) == focused &&
                read_u32(packet, 8, order) == wm_protocols &&
                read_u32(packet, 12, order) == wm_delete_window &&
                read_u32(packet, 16, order) == event_time &&
                read_u32(packet, 20, order) == 0 &&
                read_u32(packet, 24, order) == 0 &&
                read_u32(packet, 28, order) == 0,
            "WM_DELETE_WINDOW ClientMessage bytes are exact in both orders");
  }

  const auto fallback = evaluate_close_binding(
      bindings, kInteractiveMod1, kInteractiveF4Keysym, focused, true, false,
      false, event_time);
  require(fallback.action == CloseAction::DestroyTopLevel &&
              fallback.target == focused,
          "missing WM_DELETE_WINDOW selects coordinated top-level destroy");
  ResourceTable resources;
  constexpr std::uint32_t base = 0x00400000;
  constexpr std::uint32_t mask = 0x001fffff;
  require(resources.create_window(7, base, mask, window(focused)) ==
                  CreateWindowStatus::Success &&
              resources.create_window(7, base, mask, window(sibling)) ==
                  CreateWindowStatus::Success,
          "one client may own multiple top-level windows");
  const auto plan = resources.capture_destroy_plan(fallback.target);
  require(plan && plan->root == focused && plan->postorder.size() == 1 &&
              resources.commit_destroy_plan(*plan) ==
                  DestroyWindowStatus::Success &&
              !resources.find_window(focused) && resources.find_window(sibling),
          "fallback destroys only the focused top-level and preserves siblings");

  require(evaluate_close_binding(bindings, kInteractiveMod1,
                                 kInteractiveF4Keysym, 1, false, false, true,
                                 event_time)
                      .action == CloseAction::None &&
              evaluate_close_binding(bindings, kInteractiveMod1,
                                     kInteractiveF4Keysym, focused, true, true,
                                     true, event_time)
                      .action == CloseAction::None,
          "root and override-redirect windows ignore the close binding");

  InteractivePolicy geometry;
  require(geometry.begin({InteractionKind::Move, focused, 1, {10, 20},
                          {30, 40, 640, 480}, true, true, true, true})
              .accepted,
          "geometry rollback fixture begins");
  geometry.motion({20, 35});
  require(geometry.take_geometry_request() ==
                  InteractiveGeometry{40, 55, 640, 480} &&
              geometry.complete_geometry({42, 57, 640, 480}),
          "accepted policy geometry becomes the rollback point");
  geometry.motion({50, 70});
  require(geometry.take_geometry_request().has_value() && geometry.abort() &&
              geometry.kind() == InteractionKind::None &&
              geometry.cursor() == InteractionCursor::None &&
              geometry.last_committed() ==
                  InteractiveGeometry{42, 57, 640, 480},
          "rejected in-flight geometry aborts to the last committed result");
}
