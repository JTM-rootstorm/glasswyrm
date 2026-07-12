#include "protocol/x11/exposure_event.hpp"
#include "helpers/test_support.hpp"

int main() {
  using namespace gw::protocol::x11;
  const ExposeEvent expose{0x11223344U, 1, 2, 3, 4, 5};
  const auto little = encode_expose(ByteOrder::LittleEndian, 0x12345, expose);
  const auto big = encode_expose(ByteOrder::BigEndian, 0x12345, expose);
  gw::test::require(little.size() == 32U && big.size() == 32U, "event size");
  gw::test::require(little[0] == 12U && little[2] == 0x45U, "little expose");
  gw::test::require(little[4] == 0x44U && big[2] == 0x23U, "byte orders");
  gw::test::require(big[4] == 0x11U, "big window");
  const auto graphics = encode_graphics_expose(
      ByteOrder::LittleEndian, 7, {9, 1, 2, 3, 4, 0, 2, 62});
  gw::test::require(graphics[0] == 13U && graphics[20] == 62U, "graphics expose");
  const auto none = encode_no_expose(ByteOrder::BigEndian, 8, {9, 0, 62});
  gw::test::require(none[0] == 14U && none[10] == 62U, "no expose");
  return 0;
}
