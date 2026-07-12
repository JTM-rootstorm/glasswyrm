#include "protocol/x11/event.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

namespace x11 = gw::protocol::x11;

namespace {

bool expect(const std::vector<std::uint8_t>& actual,
            const std::array<std::uint8_t, 32>& expected,
            const char* label) {
  if (actual.size() == expected.size() &&
      std::equal(actual.begin(), actual.end(), expected.begin())) {
    return true;
  }
  std::cerr << "event vector mismatch: " << label << '\n';
  return false;
}

}  // namespace

int main() {
  bool ok = true;
  ok &= expect(
      x11::encode_destroy_notify(x11::ByteOrder::LittleEndian, 0x12345,
                                 {0x01020304, 0xa0b0c0d0}),
      {17, 0, 0x45, 0x23, 4, 3, 2, 1, 0xd0, 0xc0, 0xb0, 0xa0},
      "destroy little");
  ok &= expect(
      x11::encode_unmap_notify(x11::ByteOrder::BigEndian, 0x12345,
                               {0x01020304, 0xa0b0c0d0, true}),
      {18, 0, 0x23, 0x45, 1, 2, 3, 4, 0xa0, 0xb0, 0xc0, 0xd0, 1},
      "unmap big");
  ok &= expect(
      x11::encode_map_notify(x11::ByteOrder::LittleEndian, 7,
                             {0x01020304, 0xa0b0c0d0, true}),
      {19, 0, 7, 0, 4, 3, 2, 1, 0xd0, 0xc0, 0xb0, 0xa0, 1},
      "map little");
  ok &= expect(
      x11::encode_configure_notify(
          x11::ByteOrder::BigEndian, 9,
          {0x01020304, 0xa0b0c0d0, 0x11223344, -2, 3, 640, 480, 5, true}),
      {22,   0,    0,    9,    1,    2,    3,    4,
       0xa0, 0xb0, 0xc0, 0xd0, 0x11, 0x22, 0x33, 0x44,
       0xff, 0xfe, 0,    3,    2,    0x80, 1,    0xe0,
       0,    5,    1,    0},
      "configure big");
  return ok ? 0 : 1;
}
