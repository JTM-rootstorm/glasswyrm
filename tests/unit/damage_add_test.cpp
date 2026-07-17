#include "glasswyrmd/extension_registry.hpp"
#include "glasswyrmd/request_dispatcher.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace {

using namespace glasswyrm::server;
namespace x11 = gw::protocol::x11;
namespace geometry = glasswyrm::geometry;
using gw::test::require;

x11::ByteWriter header(const x11::ByteOrder order, const std::uint8_t opcode,
                       const std::uint8_t minor, const std::uint16_t units) {
  x11::ByteWriter writer(order);
  writer.write_u8(opcode);
  writer.write_u8(minor);
  writer.write_u16(units);
  return writer;
}

x11::FramedRequest finish(x11::ByteWriter writer, const std::uint8_t opcode,
                          const std::uint8_t minor) {
  x11::FramedRequest request;
  request.opcode = opcode;
  request.data = minor;
  request.bytes = std::move(writer).take();
  request.length_units =
      static_cast<std::uint32_t>(request.bytes.size() / 4U);
  return request;
}

void rectangle(x11::ByteWriter& writer, const std::int16_t x,
               const std::int16_t y, const std::uint16_t width,
               const std::uint16_t height) {
  writer.write_u16(std::bit_cast<std::uint16_t>(x));
  writer.write_u16(std::bit_cast<std::uint16_t>(y));
  writer.write_u16(width);
  writer.write_u16(height);
}

DispatchResult create_region(ServerState& state, const DispatchContext& context,
                             const std::uint32_t xid,
                             const std::span<const geometry::Rectangle> values) {
  auto writer = header(context.byte_order, 130, 5,
                       static_cast<std::uint16_t>(2 + values.size() * 2));
  writer.write_u32(xid);
  for (const auto value : values)
    rectangle(writer, static_cast<std::int16_t>(value.x),
              static_cast<std::int16_t>(value.y),
              static_cast<std::uint16_t>(value.width),
              static_cast<std::uint16_t>(value.height));
  return dispatch_request(state, context, finish(std::move(writer), 130, 5));
}

DispatchResult create_damage(ServerState& state, const DispatchContext& context,
                             const std::uint32_t xid,
                             const std::uint32_t drawable,
                             const std::uint8_t level) {
  auto writer = header(context.byte_order, 131, 1, 4);
  writer.write_u32(xid);
  writer.write_u32(drawable);
  writer.write_u8(level);
  writer.write_padding(3);
  return dispatch_request(state, context, finish(std::move(writer), 131, 1));
}

DispatchResult add(ServerState& state, const DispatchContext& context,
                   const std::uint32_t drawable,
                   const std::uint32_t region) {
  auto writer = header(context.byte_order, 131, 4, 3);
  writer.write_u32(drawable);
  writer.write_u32(region);
  return dispatch_request(state, context, finish(std::move(writer), 131, 4));
}

const DamageNotifyEvent* damage_event(const DispatchResult& result) {
  if (result.protocol_events.size() != 1) return nullptr;
  return std::get_if<DamageNotifyEvent>(&result.protocol_events.front().event);
}

void test_add_and_bounding_box(const x11::ByteOrder order) {
  const ExtensionRegistry extensions(true, {});
  ServerState state;
  DispatchContext context{1, 0x400000, 0x1fffff, 301, order, false,
                          InputSnapshot{0, 0, 0, 1, 700}, &extensions, {}};
  constexpr std::uint32_t pixmap = 0x400001;
  constexpr std::uint32_t damage = 0x400002;
  constexpr std::uint32_t first = 0x400010;
  constexpr std::uint32_t contained = 0x400011;
  constexpr std::uint32_t expanded = 0x400012;
  constexpr std::uint32_t multi = 0x400013;
  require(state.resources().create_pixmap(
              1, context.resource_base, context.resource_mask, pixmap,
              state.screen().root_window, 24, 16, 16) ==
              CreatePixmapStatus::Success,
          "create DamageAdd drawable");

  for (const auto level : {std::uint8_t{0}, std::uint8_t{1}}) {
    const auto rejected = create_damage(state, context, damage, pixmap, level);
    require(rejected.output[1] ==
                static_cast<std::uint8_t>(x11::CoreErrorCode::BadValue) &&
                !state.resources().find_damage(damage),
            "Raw and Delta report levels are rejected atomically");
  }
  require(create_damage(state, context, damage, pixmap, 2).output.empty(),
          "create BoundingBox damage resource");
  const std::array first_rectangles{geometry::Rectangle{2, 2, 2, 2}};
  const std::array contained_rectangles{geometry::Rectangle{2, 2, 1, 1}};
  const std::array expanded_rectangles{geometry::Rectangle{8, 2, 2, 2}};
  const std::array multi_rectangles{geometry::Rectangle{0, 0, 1, 1},
                                    geometry::Rectangle{12, 12, 1, 1}};
  require(create_region(state, context, first, first_rectangles).output.empty() &&
              create_region(state, context, contained, contained_rectangles)
                  .output.empty() &&
              create_region(state, context, expanded, expanded_rectangles)
                  .output.empty() &&
              create_region(state, context, multi, multi_rectangles)
                  .output.empty(),
          "create DamageAdd regions");

  auto result = add(state, context, pixmap, first);
  const auto* notify = damage_event(result);
  require(notify && notify->level == 2 &&
              notify->area == geometry::Rectangle{2, 2, 2, 2},
          "first BoundingBox add notifies its accumulated bounds");
  result = add(state, context, pixmap, contained);
  require(result.protocol_events.empty(),
          "contained add does not grow or re-notify BoundingBox");
  result = add(state, context, pixmap, expanded);
  notify = damage_event(result);
  require(notify && notify->area == geometry::Rectangle{2, 2, 8, 2},
          "BoundingBox notifies only when accumulated extents grow");
  result = add(state, context, pixmap, multi);
  notify = damage_event(result);
  require(notify && result.protocol_events.size() == 1 &&
              notify->area == geometry::Rectangle{0, 0, 13, 13},
          "one multi-rectangle DamageAdd emits one bounding notification");

  result = add(state, context, 0x400099, first);
  require(result.output[1] ==
              static_cast<std::uint8_t>(x11::CoreErrorCode::BadDrawable),
          "DamageAdd validates drawable before mutation");
  result = add(state, context, pixmap, 0x400099);
  require(result.output[1] == 129 && result.output[10] == 131,
          "DamageAdd reports XFIXES BadRegion for unknown region");
}

void test_nonempty_subtract_notify(const x11::ByteOrder order) {
  const ExtensionRegistry extensions(true, {});
  ServerState state;
  DispatchContext context{1, 0x400000, 0x1fffff, 302, order, false,
                          InputSnapshot{0, 0, 0, 1, 701}, &extensions, {}};
  constexpr std::uint32_t pixmap = 0x400021;
  constexpr std::uint32_t damage = 0x400022;
  constexpr std::uint32_t damaged = 0x400023;
  constexpr std::uint32_t repair = 0x400024;
  require(state.resources().create_pixmap(
              1, context.resource_base, context.resource_mask, pixmap,
              state.screen().root_window, 24, 16, 16) ==
                  CreatePixmapStatus::Success &&
              create_damage(state, context, damage, pixmap, 3).output.empty(),
          "create NonEmpty DamageAdd fixture");
  const std::array damaged_rectangles{geometry::Rectangle{0, 0, 2, 2},
                                      geometry::Rectangle{8, 8, 2, 2}};
  const std::array repair_rectangles{geometry::Rectangle{0, 0, 2, 2}};
  require(create_region(state, context, damaged, damaged_rectangles)
                  .output.empty() &&
              create_region(state, context, repair, repair_rectangles)
                  .output.empty(),
          "create NonEmpty damage and repair regions");
  require(damage_event(add(state, context, pixmap, damaged)) != nullptr,
          "empty-to-nonempty DamageAdd notifies once");
  require(add(state, context, pixmap, damaged).protocol_events.empty(),
          "repeat NonEmpty DamageAdd remains suppressed");

  auto writer = header(order, 131, 3, 4);
  writer.write_u32(damage);
  writer.write_u32(repair);
  writer.write_u32(0);
  const auto result = dispatch_request(
      state, context, finish(std::move(writer), 131, 3));
  const auto* notify = damage_event(result);
  require(notify && notify->area == geometry::Rectangle{8, 8, 2, 2} &&
              state.resources().find_damage(damage)->accumulated ==
                  std::vector<geometry::Rectangle>{{8, 8, 2, 2}},
          "partial DamageSubtract reports the remaining NonEmpty area");
}

}  // namespace

int main() {
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian}) {
    test_add_and_bounding_box(order);
    test_nonempty_subtract_notify(order);
  }
  return 0;
}
