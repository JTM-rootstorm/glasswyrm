#include "protocol/x11/setup.hpp"
#include "tests/helpers/test_support.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using gw::protocol::x11::ByteOrder;
using gw::test::require;
using gw::test::require_bytes_equal;

std::vector<std::uint8_t> decode_hex(const std::string_view hex) {
  require(hex.size() % 2 == 0, "golden hex has complete bytes");
  auto nibble = [](const char character) -> std::uint8_t {
    if (character >= '0' && character <= '9') {
      return static_cast<std::uint8_t>(character - '0');
    }
    if (character >= 'a' && character <= 'f') {
      return static_cast<std::uint8_t>(character - 'a' + 10);
    }
    throw std::invalid_argument("invalid golden hex digit");
  };

  std::vector<std::uint8_t> bytes;
  bytes.reserve(hex.size() / 2);
  for (std::size_t index = 0; index < hex.size(); index += 2) {
    bytes.push_back(static_cast<std::uint8_t>((nibble(hex[index]) << 4U) |
                                              nibble(hex[index + 1])));
  }
  return bytes;
}

std::uint16_t read_u16(const std::span<const std::uint8_t> bytes,
                       const std::size_t offset, const ByteOrder order) {
  require(offset + 2 <= bytes.size(), "u16 field is in bounds");
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
  if (order == ByteOrder::LittleEndian) {
    for (std::size_t index = 0; index < 4; ++index) {
      value |= static_cast<std::uint32_t>(bytes[offset + index])
               << (index * 8U);
    }
  } else {
    for (std::size_t index = 0; index < 4; ++index) {
      value = (value << 8U) | bytes[offset + index];
    }
  }
  return value;
}

void test_success_golden(const ByteOrder order, const std::string_view golden) {
  const auto reply = gw::protocol::x11::encode_setup_success(order);
  require_bytes_equal(reply, decode_hex(golden), "setup success golden bytes");
  require(reply.size() == 144, "success reply has complete synthetic screen");
  require(reply[0] == 1, "success status is encoded");
  require(read_u16(reply, 2, order) == 11 && read_u16(reply, 4, order) == 0,
          "success reply advertises X11 11.0");
  require(std::size_t{8} + read_u16(reply, 6, order) * std::size_t{4} ==
              reply.size(),
          "success length field matches encoded body");
  require(reply[28] == 1 && reply[29] == 1,
          "one screen and one pixmap format are encoded");
  require(reply[30] == 0 && reply[31] == 0,
          "synthetic image and bitmap formats remain LSBFirst");
  require(reply[64] == 24 && reply[65] == 32,
          "depth-24 pixmaps use 32 bits per pixel");
  require(reply[110] == 24 && reply[111] == 1,
          "screen has root depth 24 and one allowed depth");
  require(reply[112] == 24 && read_u16(reply, 114, order) == 1,
          "allowed depth has one visual");
  require(reply[124] == 4 && reply[125] == 8 &&
              read_u16(reply, 126, order) == 256,
          "visual is TrueColor with the required RGB precision");
  require(read_u32(reply, 128, order) == 0x00ff0000 &&
              read_u32(reply, 132, order) == 0x0000ff00 &&
              read_u32(reply, 136, order) == 0x000000ff,
          "visual masks are conventional RGB masks");
  require((read_u32(reply, 72, order) & 0x00400000U) == 0 &&
              (read_u32(reply, 76, order) & 0x00400000U) == 0 &&
              (read_u32(reply, 104, order) & 0x00400000U) == 0,
          "server objects are outside the client resource-ID range");
}

void test_failure_golden(const ByteOrder order, const std::string_view golden) {
  constexpr std::string_view reason = "Unsupported protocol version";
  const auto reply = gw::protocol::x11::encode_setup_failure(order, reason);
  require_bytes_equal(reply, decode_hex(golden), "setup failure golden bytes");
  require(reply[0] == 0 && reply[1] == reason.size(),
          "failure status and reason length are encoded");
  require(read_u16(reply, 2, order) == 11 && read_u16(reply, 4, order) == 0,
          "failure reply advertises X11 11.0");
  require(std::size_t{8} + read_u16(reply, 6, order) * std::size_t{4} ==
              reply.size(),
          "failure length field matches padded reason");
}

void test_configurable_resource_range() {
  gw::protocol::x11::SetupReplyConfig config;
  config.resource_id_base = 0x00800000;
  config.resource_id_mask = 0x000fffff;
  const auto reply =
      gw::protocol::x11::encode_setup_success(ByteOrder::BigEndian, config);
  require(
      read_u32(reply, 12, ByteOrder::BigEndian) == config.resource_id_base &&
          read_u32(reply, 16, ByteOrder::BigEndian) == config.resource_id_mask,
      "per-client resource range is encoded");
}

void test_failure_reason_limit() {
  const std::vector<char> oversized(256, 'x');
  bool threw = false;
  try {
    (void)gw::protocol::x11::encode_setup_failure(
        ByteOrder::LittleEndian,
        std::string_view(oversized.data(), oversized.size()));
  } catch (const std::length_error &) {
    threw = true;
  }
  require(threw, "failure reasons larger than the wire field are rejected");
}

} // namespace

int main() {
  test_success_golden(
      ByteOrder::LittleEndian,
      "01000b00000022000100000000004000ffff1f00000000001500ffff01010000202008ff"
      "00000000476c6173737779726d204d696c6573746f6e6520310000001820200000000000"
      "0100000002000000ffffff000000000000000000000400030e01cb000100010003000000"
      "00001801180001000000000003000000040800010000ff0000ff0000ff0000000000000"
      "0");
  test_success_golden(
      ByteOrder::BigEndian,
      "0100000b000000220000000100400000001fffff000000000015ffff01010000202008ff"
      "00000000476c6173737779726d204d696c6573746f6e6520310000001820200000000000"
      "000000010000000200ffffff000000000000000004000300010e00cb0001000100000003"
      "000018011800000100000000000000030408010000ff00000000ff00000000ff0000000"
      "0");
  test_failure_golden(ByteOrder::LittleEndian,
                      "001c0b0000000700556e737570706f727465642070726f746f636f6c"
                      "2076657273696f6e");
  test_failure_golden(ByteOrder::BigEndian,
                      "001c000b00000007556e737570706f727465642070726f746f636f6c"
                      "2076657273696f6e");
  test_configurable_resource_range();
  test_failure_reason_limit();
  return 0;
}
