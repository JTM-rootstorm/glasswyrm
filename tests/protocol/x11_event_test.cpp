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
      x11::encode_unmap_notify(x11::ByteOrder::LittleEndian, 7,
                               {0x01020304, 0xa0b0c0d0, false, true}),
      {0x92, 0, 7, 0, 4, 3, 2, 1, 0xd0, 0xc0, 0xb0, 0xa0},
      "synthetic unmap little");
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
  ok &= expect(
      x11::encode_property_notify(
          x11::ByteOrder::LittleEndian, 0x12345,
          {0x01020304, 0xa0b0c0d0, 0x11223344,
           x11::PropertyNotifyState::Deleted}),
      {28, 0, 0x45, 0x23, 4, 3, 2, 1, 0xd0, 0xc0, 0xb0, 0xa0,
       0x44, 0x33, 0x22, 0x11, 1},
      "property little");
  ok &= expect(
      x11::encode_selection_clear(x11::ByteOrder::BigEndian, 9,
                                  {0x01020304, 0x11223344, 0xa0b0c0d0}),
      {29, 0, 0, 9, 1, 2, 3, 4, 0x11, 0x22, 0x33, 0x44,
       0xa0, 0xb0, 0xc0, 0xd0},
      "selection clear big");
  ok &= expect(
      x11::encode_selection_request(
          x11::ByteOrder::BigEndian, 10,
          {1, 2, 3, 4, 5, 6}),
      {30, 0, 0, 10, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3,
       0, 0, 0, 4, 0, 0, 0, 5, 0, 0, 0, 6},
      "selection request big");
  ok &= expect(
      x11::encode_selection_notify(
          x11::ByteOrder::LittleEndian, 11,
          {1, 2, 3, 4, 5, true}),
      {static_cast<std::uint8_t>(31 | 0x80), 0, 11, 0,
       1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0,
       4, 0, 0, 0, 5, 0, 0, 0},
      "selection notify synthetic little");
  ok &= expect(
      x11::encode_client_message(
          x11::ByteOrder::BigEndian, 12,
          {0x01020304, 0xa0b0c0d0,
           std::array<std::uint16_t, 10>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10},
           true}),
      {static_cast<std::uint8_t>(33 | 0x80), 16, 0, 12,
       1, 2, 3, 4, 0xa0, 0xb0, 0xc0, 0xd0,
       0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0, 8, 0, 9, 0, 10},
      "client message 16 big");
  ok &= expect(
      x11::encode_client_message(
          x11::ByteOrder::LittleEndian, 13,
          {1, 2, std::array<std::uint32_t, 5>{0x01020304, 5, 6, 7, 8},
           false}),
      {33, 32, 13, 0, 1, 0, 0, 0, 2, 0, 0, 0,
       4, 3, 2, 1, 5, 0, 0, 0, 6, 0, 0, 0,
       7, 0, 0, 0, 8, 0, 0, 0},
      "client message 32 little");
  const auto client8 = x11::encode_client_message(
      x11::ByteOrder::LittleEndian, 14,
      {1, 2,
       std::array<std::uint8_t, 20>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                    10, 11, 12, 13, 14, 15, 16, 17, 18, 19},
       false});
  ok &= client8.size() == 32 && client8[1] == 8 &&
        std::equal(client8.begin() + 12, client8.end(),
                   std::array<std::uint8_t, 20>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                                10, 11, 12, 13, 14, 15, 16,
                                                17, 18, 19}.begin());
  return ok ? 0 : 1;
}
