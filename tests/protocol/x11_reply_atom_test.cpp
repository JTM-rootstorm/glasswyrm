#include "protocol/x11/atoms.hpp"
#include "protocol/x11/reply.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

using gw::protocol::x11::ByteOrder;
using gw::protocol::x11::CoreError;
using gw::protocol::x11::CoreErrorCode;
using gw::protocol::x11::ReplyBuilder;
using gw::test::require;

std::uint16_t read_u16(const std::span<const std::uint8_t> bytes,
                       const std::size_t offset, const ByteOrder order) {
  if (order == ByteOrder::LittleEndian) {
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(bytes[offset + 1] << 8U);
  }
  return static_cast<std::uint16_t>(bytes[offset] << 8U) |
         static_cast<std::uint16_t>(bytes[offset + 1]);
}

std::uint32_t read_u32(const std::span<const std::uint8_t> bytes,
                       const std::size_t offset, const ByteOrder order) {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < 4; ++index) {
    const auto shift = order == ByteOrder::LittleEndian ? index * 8U
                                                        : (3 - index) * 8U;
    value |= static_cast<std::uint32_t>(bytes[offset + index]) << shift;
  }
  return value;
}

void test_reply(const ByteOrder order) {
  ReplyBuilder builder(order, 0x10001, 24);
  builder.write_u32(1);
  builder.write_u16(1024);
  builder.write_u16(768);
  builder.write_payload_u32(0x11223344);
  const auto reply = std::move(builder).finish();

  require(reply.size() == 36 && reply[0] == 1 && reply[1] == 24,
          "reply has a 32-byte header and aligned payload");
  require(read_u16(reply, 2, order) == 1,
          "reply sequence uses the low 16 bits");
  require(read_u32(reply, 4, order) == 1,
          "reply length counts only payload units");
  require(read_u32(reply, 8, order) == 1 &&
              read_u16(reply, 12, order) == 1024 &&
              read_u16(reply, 14, order) == 768,
          "fixed reply fields use client byte order");
  require(read_u32(reply, 32, order) == 0x11223344,
          "typed payload fields use client byte order");

  ReplyBuilder padded(order, 5);
  const std::array<std::uint8_t, 3> payload{1, 2, 3};
  padded.write_payload(payload);
  const auto padded_reply = std::move(padded).finish();
  require(padded_reply.size() == 36 && read_u32(padded_reply, 4, order) == 1 &&
              padded_reply[35] == 0,
          "variable payload is padded and measured in four-byte units");
}

void test_error(const ByteOrder order) {
  const CoreError error{CoreErrorCode::BadAtom, 0x10002, 0xa1b2c3d4, 17, 0};
  const auto packet = gw::protocol::x11::encode_core_error(order, error);
  require(packet.size() == 32 && packet[0] == 0 && packet[1] == 5,
          "core error response and code are encoded");
  require(read_u16(packet, 2, order) == 2 &&
              read_u32(packet, 4, order) == error.bad_value &&
              read_u16(packet, 8, order) == 0 && packet[10] == 17,
          "core error fields use the normative layout");
  for (std::size_t index = 11; index < packet.size(); ++index) {
    require(packet[index] == 0, "core error padding is zeroed");
  }
}

void test_atoms() {
  constexpr std::array<std::string_view, 68> names{
      "PRIMARY", "SECONDARY", "ARC", "ATOM", "BITMAP", "CARDINAL",
      "COLORMAP", "CURSOR", "CUT_BUFFER0", "CUT_BUFFER1", "CUT_BUFFER2",
      "CUT_BUFFER3", "CUT_BUFFER4", "CUT_BUFFER5", "CUT_BUFFER6",
      "CUT_BUFFER7", "DRAWABLE", "FONT", "INTEGER", "PIXMAP", "POINT",
      "RECTANGLE", "RESOURCE_MANAGER", "RGB_COLOR_MAP", "RGB_BEST_MAP",
      "RGB_BLUE_MAP", "RGB_DEFAULT_MAP", "RGB_GRAY_MAP", "RGB_GREEN_MAP",
      "RGB_RED_MAP", "STRING", "VISUALID", "WINDOW", "WM_COMMAND",
      "WM_HINTS", "WM_CLIENT_MACHINE", "WM_ICON_NAME", "WM_ICON_SIZE",
      "WM_NAME", "WM_NORMAL_HINTS", "WM_SIZE_HINTS", "WM_ZOOM_HINTS",
      "MIN_SPACE", "NORM_SPACE", "MAX_SPACE", "END_SPACE",
      "SUPERSCRIPT_X", "SUPERSCRIPT_Y", "SUBSCRIPT_X", "SUBSCRIPT_Y",
      "UNDERLINE_POSITION", "UNDERLINE_THICKNESS", "STRIKEOUT_ASCENT",
      "STRIKEOUT_DESCENT", "ITALIC_ANGLE", "X_HEIGHT", "QUAD_WIDTH",
      "WEIGHT", "POINT_SIZE", "RESOLUTION", "COPYRIGHT", "NOTICE",
      "FONT_NAME", "FAMILY_NAME", "FULL_NAME", "CAP_HEIGHT", "WM_CLASS",
      "WM_TRANSIENT_FOR"};
  require(gw::protocol::x11::kNoneAtom == 0 &&
              gw::protocol::x11::kLastPredefinedAtom == 68,
          "the predefined atom range ends at WM_TRANSIENT_FOR");
  for (std::size_t index = 0; index < names.size(); ++index) {
    require(gw::protocol::x11::kPredefinedAtoms[index].id == index + 1 &&
                gw::protocol::x11::kPredefinedAtoms[index].name == names[index],
            "predefined atom IDs and names match xproto.xml");
  }
}

void test_builder_limit() {
  ReplyBuilder builder(ByteOrder::LittleEndian, 1);
  bool threw = false;
  try {
    builder.write_padding(25);
  } catch (const std::length_error &) {
    threw = true;
  }
  require(threw, "reply fixed fields cannot exceed the protocol header");
}

} // namespace

int main() {
  test_reply(ByteOrder::LittleEndian);
  test_reply(ByteOrder::BigEndian);
  test_error(ByteOrder::LittleEndian);
  test_error(ByteOrder::BigEndian);
  test_atoms();
  test_builder_limit();
  require(gw::protocol::x11::wire_sequence(0xffff) == 0xffff &&
              gw::protocol::x11::wire_sequence(0x10000) == 0 &&
              gw::protocol::x11::wire_sequence(0x10001) == 1,
          "wire sequence wraps at the 16-bit boundary");
  return 0;
}
