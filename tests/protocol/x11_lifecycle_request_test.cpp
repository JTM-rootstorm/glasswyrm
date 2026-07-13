#include "protocol/x11/lifecycle_request.hpp"

#include "protocol/x11/byte_cursor.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
namespace x11 = gw::protocol::x11;
void require(bool value, const char* message) {
  if (!value) { std::fprintf(stderr, "x11 lifecycle decoder: %s\n", message); std::exit(1); }
}
std::vector<std::uint8_t> simple(x11::ByteOrder order, x11::CoreOpcode opcode,
                                 std::uint32_t window) {
  x11::ByteWriter writer(order); writer.write_u8(static_cast<std::uint8_t>(opcode));
  writer.write_u8(0); writer.write_u16(2); writer.write_u32(window);
  return std::move(writer).take();
}
std::vector<std::uint8_t> configure(x11::ByteOrder order, std::uint16_t mask,
                                    const std::vector<std::uint32_t>& values) {
  x11::ByteWriter writer(order); writer.write_u8(12); writer.write_u8(0);
  writer.write_u16(static_cast<std::uint16_t>(3 + values.size()));
  writer.write_u32(0x10203040); writer.write_u16(mask); writer.write_u16(0);
  for (auto value : values) writer.write_u32(value);
  return std::move(writer).take();
}
void test_simple() {
  for (auto order : {x11::ByteOrder::LittleEndian, x11::ByteOrder::BigEndian}) {
    x11::WindowLifecycleRequest request;
    auto bytes = simple(order, x11::CoreOpcode::MapWindow, 0x12345678);
    require(x11::decode_map_window(bytes, order, request) == x11::LifecycleDecodeStatus::Complete && request.window == 0x12345678, "MapWindow both byte orders");
    bytes = simple(order, x11::CoreOpcode::MapSubwindows, 0x12345678);
    require(x11::decode_map_subwindows(bytes, order, request) == x11::LifecycleDecodeStatus::Complete && request.window == 0x12345678, "MapSubwindows both byte orders");
    bytes = simple(order, x11::CoreOpcode::UnmapWindow, 9);
    require(x11::decode_unmap_window(bytes, order, request) == x11::LifecycleDecodeStatus::Complete && request.window == 9, "UnmapWindow both byte orders");
    bytes = simple(order, x11::CoreOpcode::UnmapSubwindows, 9);
    require(x11::decode_unmap_subwindows(bytes, order, request) == x11::LifecycleDecodeStatus::Complete && request.window == 9, "UnmapSubwindows both byte orders");
    bytes.pop_back(); require(x11::decode_unmap_window(bytes, order, request) == x11::LifecycleDecodeStatus::BadLength, "simple exact length");
  }
}
void test_configure() {
  constexpr auto mask = static_cast<std::uint16_t>(x11::ConfigureX | x11::ConfigureY | x11::ConfigureWidth | x11::ConfigureHeight | x11::ConfigureBorderWidth | x11::ConfigureSibling | x11::ConfigureStackMode);
  for (auto order : {x11::ByteOrder::LittleEndian, x11::ByteOrder::BigEndian}) {
    auto bytes = configure(order, mask, {UINT32_C(0xfffffffe), 7, 640, 480, 3, 99, 1});
    x11::ConfigureWindowRequest request;
    require(x11::decode_configure_window(bytes, order, request) == x11::LifecycleDecodeStatus::Complete && request.x == -2 && request.y == 7 && request.width == 640 && request.stack_mode == x11::CoreStackMode::Below, "ConfigureWindow ordered fields both byte orders");
  }
  x11::ConfigureWindowRequest request;
  require(x11::decode_configure_window(configure(x11::ByteOrder::LittleEndian, x11::ConfigureWidth, {0}), x11::ByteOrder::LittleEndian, request) == x11::LifecycleDecodeStatus::BadValue, "zero width rejected");
  require(x11::decode_configure_window(configure(x11::ByteOrder::LittleEndian, 0x80, {1}), x11::ByteOrder::LittleEndian, request) == x11::LifecycleDecodeStatus::BadValue, "unknown mask rejected");
  require(x11::decode_configure_window(configure(x11::ByteOrder::LittleEndian, x11::ConfigureSibling, {99}), x11::ByteOrder::LittleEndian, request) == x11::LifecycleDecodeStatus::BadMatch, "sibling requires mode");
  require(x11::decode_configure_window(configure(x11::ByteOrder::LittleEndian, x11::ConfigureStackMode, {5}), x11::ByteOrder::LittleEndian, request) == x11::LifecycleDecodeStatus::BadValue, "unknown stack mode rejected");
  require(x11::decode_configure_window(configure(x11::ByteOrder::LittleEndian, static_cast<std::uint16_t>(x11::ConfigureSibling | x11::ConfigureStackMode), {0x10203040, 0}), x11::ByteOrder::LittleEndian, request) == x11::LifecycleDecodeStatus::BadMatch, "self sibling rejected");
}
}
int main() { test_simple(); test_configure(); }
