#include "glasswyrmd/request_dispatcher.hpp"
#include "helpers/test_support.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/core.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace x11 = gw::protocol::x11;
using namespace glasswyrm::server;
using gw::test::require;

namespace {

x11::FramedRequest request(const x11::ByteOrder order,
                           const x11::CoreOpcode opcode,
                           const std::uint8_t first = 0,
                           const std::uint8_t count = 0) {
  x11::ByteWriter writer(order);
  writer.write_u8(static_cast<std::uint8_t>(opcode));
  writer.write_u8(first);
  writer.write_u16(opcode == x11::CoreOpcode::GetKeyboardMapping ? 2 : 1);
  if (opcode == x11::CoreOpcode::GetKeyboardMapping) {
    writer.write_u8(first);
    writer.write_u8(count);
    writer.write_u16(0);
  }
  x11::FramedRequest result;
  result.opcode = static_cast<std::uint8_t>(opcode);
  result.data = first;
  result.bytes = std::move(writer).take();
  result.length_units = static_cast<std::uint16_t>(result.bytes.size() / 4U);
  return result;
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes,
                       const std::size_t offset,
                       const x11::ByteOrder order) {
  x11::ByteReader reader(
      std::span<const std::uint8_t>(bytes).subspan(offset), order);
  std::uint32_t value{};
  require(reader.read_u32(value), "mapping reply contains u32");
  return value;
}

}  // namespace

int main() {
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian}) {
    ServerState state;
    DispatchContext fixed{1, 0x400000, 0x1fffff, 7, order};
    auto result = dispatch_request(
        state, fixed,
        request(order, x11::CoreOpcode::GetKeyboardMapping, 38, 1));
    require(result.output.size() == 40 && result.output[1] == 2 &&
                read_u32(result.output, 32, order) == 'a' &&
                read_u32(result.output, 36, order) == 'A',
            "fixed profile retains the historical two-keysym reply");
    result = dispatch_request(
        state, fixed, request(order, x11::CoreOpcode::GetModifierMapping));
    constexpr std::array<std::uint8_t, 16> fixed_modifiers{
        50, 62, 0, 0, 37, 105, 64, 108, 0, 0, 0, 0, 0, 0, 0, 0};
    require(result.output.size() == 48 && result.output[1] == 2 &&
                std::equal(fixed_modifiers.begin(), fixed_modifiers.end(),
                           result.output.begin() + 32),
            "fixed modifier reply bytes remain unchanged");

    auto mapping = std::make_shared<KeyboardMappingSnapshot>();
    mapping->keysyms_per_keycode = 4;
    mapping->keysyms.resize(256U * 4U);
    mapping->keysyms[38U * 4U] = 'a';
    mapping->keysyms[38U * 4U + 1U] = 'A';
    mapping->keysyms[38U * 4U + 2U] = 0x100abcdU;
    mapping->keycodes_per_modifier = 3;
    mapping->modifier_keycodes.resize(24);
    mapping->modifier_keycodes[0] = 50;
    mapping->modifier_keycodes[1] = 62;
    mapping->modifier_keycodes[6] = 37;
    mapping->modifier_keycodes[7] = 105;
    DispatchContext real = fixed;
    real.input.keyboard_mapping = mapping;
    result = dispatch_request(
        state, real,
        request(order, x11::CoreOpcode::GetKeyboardMapping, 38, 1));
    require(result.output.size() == 48 && result.output[1] == 4 &&
                read_u32(result.output, 32, order) == 'a' &&
                read_u32(result.output, 36, order) == 'A' &&
                read_u32(result.output, 40, order) == 0x100abcdU &&
                read_u32(result.output, 44, order) == 0,
            "real profile serializes four compiled keysyms in byte order");
    result = dispatch_request(
        state, real, request(order, x11::CoreOpcode::GetModifierMapping));
    require(result.output.size() == 56 && result.output[1] == 3 &&
                std::equal(mapping->modifier_keycodes.begin(),
                           mapping->modifier_keycodes.end(),
                           result.output.begin() + 32),
            "real profile serializes the derived modifier map");
  }
}
