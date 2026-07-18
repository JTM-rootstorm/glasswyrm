#include "glasswyrmd/extension_registry.hpp"
#include "glasswyrmd/extension_event_helpers.hpp"
#include "glasswyrmd/request_dispatcher.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace {

using namespace glasswyrm::server;
namespace x11 = gw::protocol::x11;
namespace geometry = glasswyrm::geometry;
using gw::test::require;

x11::FramedRequest finish(x11::ByteWriter writer, const std::uint8_t opcode,
                          const std::uint8_t data) {
  x11::FramedRequest request;
  request.opcode = opcode;
  request.data = data;
  request.bytes = std::move(writer).take();
  request.length_units = request.bytes.size() / 4U;
  return request;
}

x11::ByteWriter header(const x11::ByteOrder order, const std::uint8_t opcode,
                       const std::uint8_t data, const std::uint16_t units) {
  x11::ByteWriter writer(order);
  writer.write_u8(opcode);
  writer.write_u8(data);
  writer.write_u16(units);
  return writer;
}

std::uint16_t read_u16(const std::span<const std::uint8_t> bytes,
                       const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(bytes.subspan(offset), order);
  std::uint16_t value{};
  require(reader.read_u16(value), "decode protocol u16");
  return value;
}

std::uint32_t read_u32(const std::span<const std::uint8_t> bytes,
                       const x11::ByteOrder order, const std::size_t offset) {
  x11::ByteReader reader(bytes.subspan(offset), order);
  std::uint32_t value{};
  require(reader.read_u32(value), "decode protocol u32");
  return value;
}

void write_rectangle(x11::ByteWriter& writer, const std::int16_t x,
                     const std::int16_t y, const std::uint16_t width,
                     const std::uint16_t height) {
  writer.write_u16(static_cast<std::uint16_t>(x));
  writer.write_u16(static_cast<std::uint16_t>(y));
  writer.write_u16(width);
  writer.write_u16(height);
}

x11::FramedRequest region_request(const x11::ByteOrder order,
                                  const std::uint8_t minor,
                                  const std::uint32_t xid,
                                  const geometry::Rectangle rectangle) {
  auto writer = header(order, 130, minor, 4);
  writer.write_u32(xid);
  write_rectangle(writer, static_cast<std::int16_t>(rectangle.x),
                  static_cast<std::int16_t>(rectangle.y),
                  static_cast<std::uint16_t>(rectangle.width),
                  static_cast<std::uint16_t>(rectangle.height));
  return finish(std::move(writer), 130, minor);
}

x11::FramedRequest region_ids(const x11::ByteOrder order,
                              const std::uint8_t minor,
                              const std::initializer_list<std::uint32_t> ids) {
  auto writer = header(order, 130, minor,
                       static_cast<std::uint16_t>(1 + ids.size()));
  for (const auto id : ids) writer.write_u32(id);
  return finish(std::move(writer), 130, minor);
}

void create_window(ServerState& state, const ClientId owner,
                   const std::uint32_t xid) {
  WindowCreateSpec spec;
  spec.xid = xid;
  spec.parent = state.screen().root_window;
  spec.width = 16;
  spec.height = 16;
  spec.depth = 24;
  spec.window_class = WindowClass::InputOutput;
  spec.visual = state.screen().root_visual;
  spec.attributes.colormap = state.screen().default_colormap;
  require(state.resources().create_window(owner, 0x400000, 0x1fffff, spec) ==
              CreateWindowStatus::Success,
          "create extension test window");
}

void test_versions_and_regions(const x11::ByteOrder order) {
  const ExtensionRegistry extensions(true, {});
  ServerState state;
  DispatchContext context{1, 0x400000, 0x1fffff, 17, order, false,
                          InputSnapshot{0, 0, 0, 1, 900}, &extensions, {}};

  auto writer = header(order, 130, 0, 3);
  writer.write_u32(6);
  writer.write_u32(0);
  auto result = dispatch_request(state, context,
                                 finish(std::move(writer), 130, 0));
  require(result.output.size() == 32 && read_u32(result.output, order, 8) == 2 &&
              read_u32(result.output, order, 12) == 0,
          "XFIXES negotiates no higher than 2.0");

  writer = header(order, 131, 0, 3);
  writer.write_u32(1);
  writer.write_u32(9);
  result = dispatch_request(state, context,
                            finish(std::move(writer), 131, 0));
  require(result.output.size() == 32 && read_u32(result.output, order, 8) == 1 &&
              read_u32(result.output, order, 12) == 1,
          "DAMAGE negotiates no higher than 1.1");

  constexpr std::uint32_t a = 0x400100;
  constexpr std::uint32_t b = 0x400101;
  constexpr std::uint32_t destination = 0x400102;
  result = dispatch_request(state, context,
                            region_request(order, 5, a, {0, 0, 10, 10}));
  require(result.output.empty(), "create first XFIXES region");
  result = dispatch_request(state, context,
                            region_request(order, 5, b, {5, 0, 10, 10}));
  require(result.output.empty(), "create second XFIXES region");
  writer = header(order, 130, 5, 2);
  writer.write_u32(destination);
  result = dispatch_request(state, context,
                            finish(std::move(writer), 130, 5));
  require(result.output.empty(), "create empty XFIXES destination region");

  result = dispatch_request(state, context,
                            region_ids(order, 13, {a, b, destination}));
  require(result.output.empty() &&
              state.resources().find_xfixes_region(destination)
                      ->rectangles.front() == geometry::Rectangle{0, 0, 15, 10},
          "UnionRegion performs stable normalized algebra");
  result = dispatch_request(state, context,
                            region_ids(order, 14, {a, b, destination}));
  require(result.output.empty() &&
              state.resources().find_xfixes_region(destination)
                      ->rectangles.front() == geometry::Rectangle{5, 0, 5, 10},
          "IntersectRegion computes exact overlap");
  result = dispatch_request(state, context,
                            region_ids(order, 15, {a, b, destination}));
  require(result.output.empty() &&
              state.resources().find_xfixes_region(destination)
                      ->rectangles.front() == geometry::Rectangle{0, 0, 5, 10},
          "SubtractRegion computes exact remainder");

  writer = header(order, 130, 17, 3);
  writer.write_u32(destination);
  writer.write_u16(2);
  writer.write_u16(static_cast<std::uint16_t>(-1));
  result = dispatch_request(state, context,
                            finish(std::move(writer), 130, 17));
  require(result.output.empty(), "TranslateRegion accepts checked offsets");
  result = dispatch_request(state, context,
                            region_ids(order, 19, {destination}));
  require(result.output.size() == 40 && read_u16(result.output, order, 8) == 2 &&
              static_cast<std::int16_t>(read_u16(result.output, order, 10)) == -1 &&
              read_u16(result.output, order, 12) == 5 &&
              read_u16(result.output, order, 32) == 2,
          "FetchRegion returns exact extents and normalized rectangles");

  result = dispatch_request(state, context,
                            region_ids(order, 10, {destination}));
  require(result.output.empty(), "DestroyRegion succeeds once");
  result = dispatch_request(state, context,
                            region_ids(order, 19, {destination}));
  require(result.output.size() == 32 && result.output[1] == 129 &&
              result.output[10] == 130 && read_u16(result.output, order, 8) == 19,
          "unknown region reports XFIXES BadRegion metadata");
}

void test_event_wire(const x11::ByteOrder order) {
  const auto selection = encode_xfixes_selection_notify(
      order, 0x12345,
      XFixesSelectionNotifyEvent{2, 0x400010, 0x400011, 1, 900, 800});
  require(selection.size() == 32 && selection[0] == 65 && selection[1] == 2 &&
              read_u16(selection, order, 2) == 0x2345 &&
              read_u32(selection, order, 4) == 0x400010 &&
              read_u32(selection, order, 8) == 0x400011 &&
              read_u32(selection, order, 16) == 900 &&
              read_u32(selection, order, 20) == 800,
          "XFIXES SelectionNotify wire fields use recipient byte order");
  const auto damage = encode_damage_notify(
      order, 0x12346,
      DamageNotifyEvent{3, 0x400020, 0x400022, 901,
                        {1, 2, 3, 4}, {0, 0, 8, 9}});
  require(damage.size() == 32 && damage[0] == 66 && damage[1] == 3 &&
              read_u16(damage, order, 2) == 0x2346 &&
              read_u32(damage, order, 4) == 0x400020 &&
              read_u32(damage, order, 8) == 0x400022 &&
              read_u32(damage, order, 12) == 901 &&
              read_u16(damage, order, 16) == 1 &&
              read_u16(damage, order, 22) == 4 &&
              read_u16(damage, order, 28) == 8 &&
              read_u16(damage, order, 30) == 9,
          "DAMAGE Notify wire fields use recipient byte order");
}

x11::FramedRequest damage_create(const x11::ByteOrder order,
                                 const std::uint32_t damage,
                                 const std::uint32_t drawable,
                                 const std::uint8_t level) {
  auto writer = header(order, 131, 1, 4);
  writer.write_u32(damage);
  writer.write_u32(drawable);
  writer.write_u8(level);
  writer.write_padding(3);
  return finish(std::move(writer), 131, 1);
}

x11::FramedRequest put_image(const x11::ByteOrder order,
                             const std::uint32_t drawable,
                             const std::uint32_t gc) {
  auto writer = header(order, 72, 2, 10);
  writer.write_u32(drawable);
  writer.write_u32(gc);
  writer.write_u16(2);
  writer.write_u16(2);
  writer.write_u16(1);
  writer.write_u16(2);
  writer.write_u8(0);
  writer.write_u8(24);
  writer.write_padding(2);
  for (std::uint32_t pixel = 1; pixel <= 4; ++pixel)
    writer.write_u32(pixel);
  return finish(std::move(writer), 72, 2);
}

void test_selection_and_damage(const x11::ByteOrder order) {
  const ExtensionRegistry extensions(true, {});
  ServerState state;
  DispatchContext subscriber{1, 0x400000, 0x1fffff, 21, order, false,
                             InputSnapshot{0, 0, 0, 1, 1000}, &extensions, {}};
  DispatchContext owner = subscriber;
  owner.client_id = 2;
  owner.sequence = 22;
  constexpr std::uint32_t watch_window = 0x400010;
  constexpr std::uint32_t owner_window = 0x400011;
  create_window(state, 1, watch_window);
  create_window(state, 2, owner_window);

  auto writer = header(order, 130, 2, 4);
  writer.write_u32(watch_window);
  writer.write_u32(1);  // PRIMARY
  writer.write_u32(7);
  auto result = dispatch_request(state, subscriber,
                                 finish(std::move(writer), 130, 2));
  require(result.output.empty(), "SelectSelectionInput accepts PRIMARY masks");

  writer = header(order, 22, 0, 4);
  writer.write_u32(owner_window);
  writer.write_u32(1);
  writer.write_u32(0);
  result = dispatch_request(state, owner, finish(std::move(writer), 22, 0));
  const auto* selection = result.protocol_events.empty()
                              ? nullptr
                              : std::get_if<XFixesSelectionNotifyEvent>(
                                    &result.protocol_events.front().event);
  require(selection && selection->subtype == 0 &&
              selection->window == watch_window &&
              selection->owner == owner_window && selection->timestamp == 1000,
          "owner changes produce XFIXES SelectionNotify");

  writer = header(order, 4, 0, 2);
  writer.write_u32(owner_window);
  result = dispatch_request(state, owner, finish(std::move(writer), 4, 0));
  selection = nullptr;
  for (const auto& intent : result.protocol_events)
    if (const auto* event =
            std::get_if<XFixesSelectionNotifyEvent>(&intent.event))
      selection = event;
  require(selection && selection->subtype == 1 && selection->owner == 0,
          "owner-window destruction produces SelectionWindowDestroy");

  constexpr std::uint32_t pixmap = 0x400020;
  constexpr std::uint32_t gc = 0x400021;
  constexpr std::uint32_t damage = 0x400022;
  require(state.resources().create_pixmap(1, 0x400000, 0x1fffff, pixmap,
                                           state.screen().root_window, 24, 8,
                                           8) == CreatePixmapStatus::Success &&
              state.resources().create_gc(1, 0x400000, 0x1fffff, gc, pixmap,
                                          {}) == CreateGcStatus::Success,
          "create DAMAGE drawable fixture");
  result = dispatch_request(state, subscriber,
                            damage_create(order, damage, pixmap, 3));
  require(result.output.empty(), "create NonEmpty DAMAGE resource");
  result = dispatch_request(state, subscriber, put_image(order, pixmap, gc));
  const auto* notify = result.protocol_events.empty()
                           ? nullptr
                           : std::get_if<DamageNotifyEvent>(
                                 &result.protocol_events.front().event);
  require(notify && notify->damage == damage && notify->drawable == pixmap &&
              notify->level == 3 && notify->area == geometry::Rectangle{1, 2, 2, 2} &&
              notify->geometry == geometry::Rectangle{0, 0, 8, 8},
          "canonical PutImage damage produces DAMAGE Notify");
  result = dispatch_request(state, subscriber, put_image(order, pixmap, gc));
  require(result.protocol_events.empty(),
          "NonEmpty suppresses repeat events until repair");

  constexpr std::uint32_t parts = 0x400023;
  writer = header(order, 130, 5, 2);
  writer.write_u32(parts);
  require(dispatch_request(state, subscriber,
                           finish(std::move(writer), 130, 5)).output.empty(),
          "create DAMAGE parts region");
  writer = header(order, 131, 3, 4);
  writer.write_u32(damage);
  writer.write_u32(0);
  writer.write_u32(parts);
  result = dispatch_request(state, subscriber,
                            finish(std::move(writer), 131, 3));
  require(result.output.empty() && state.resources().find_damage(damage)
                                       ->accumulated.empty() &&
              !state.resources().find_xfixes_region(parts)->rectangles.empty(),
          "DamageSubtract(None, parts) returns and clears accumulated damage");
  result = dispatch_request(state, subscriber, put_image(order, pixmap, gc));
  require(result.protocol_events.size() == 1,
          "repair resets NonEmpty event eligibility");

  writer = header(order, 131, 2, 2);
  writer.write_u32(damage);
  require(dispatch_request(state, subscriber,
                           finish(std::move(writer), 131, 2)).output.empty(),
          "Destroy DAMAGE resource");
  writer = header(order, 131, 2, 2);
  writer.write_u32(damage);
  result = dispatch_request(state, subscriber,
                            finish(std::move(writer), 131, 2));
  require(result.output[1] == 130 && result.output[10] == 131 &&
              read_u16(result.output, order, 8) == 2,
          "unknown DAMAGE resource reports BadDamage metadata");

  require(state.resources().create_damage(1, 0x400000, 0x1fffff, damage,
                                           pixmap,
                                           DamageReportLevel::BoundingBox) ==
              DamageStatus::Success,
          "recreate damage for drawable lifecycle cleanup");
  require(state.resources().free_pixmap(pixmap) == FreePixmapStatus::Success &&
              !state.resources().find_damage(damage) &&
              state.resources().find_xfixes_region(parts) &&
              state.invariants_hold(),
          "drawable destruction removes DAMAGE without corrupting regions");
  const auto cleanup = state.cleanup_client(1);
  require(cleanup.resources_destroyed >= 3 &&
              state.resources().resource_count(ResourceType::XFixesRegion) == 0 &&
              state.invariants_hold(),
          "client cleanup releases XFIXES and DAMAGE resources");
}

}  // namespace

int main() {
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian}) {
    test_versions_and_regions(order);
    test_event_wire(order);
    test_selection_and_damage(order);
  }
  return 0;
}
