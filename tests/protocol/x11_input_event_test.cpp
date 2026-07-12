#include "protocol/x11/crossing_event.hpp"
#include "protocol/x11/focus_event.hpp"
#include "protocol/x11/input_event.hpp"

#include "helpers/test_support.hpp"

#include <algorithm>

using namespace gw::protocol::x11;
using gw::test::require;

int main() {
  const InputEvent key{CoreEventType::KeyPress, 38, 0x01020304, 1, 2, 3,
                       -2, 300, -12, 20, 0x0506, true};
  auto little = encode_input_event(ByteOrder::LittleEndian, 0x12345, key);
  require(little.size() == 32 && little[0] == 2 && little[1] == 38 &&
          little[2] == 0x45 && little[3] == 0x23 && little[4] == 4 &&
          little[20] == 0xfe && little[21] == 0xff && little[30] == 1,
          "little-endian common input packet fields");
  auto big = encode_input_event(ByteOrder::BigEndian, 9,
      InputEvent{CoreEventType::MotionNotify, 99, 0x01020304, 1, 2, 0, -2, 3, -4, 5, 6, true});
  require(big[0] == 6 && big[1] == 0 && big[2] == 0 && big[3] == 9 &&
          big[4] == 1 && big[5] == 2 && big[6] == 3 && big[7] == 4 &&
          big[20] == 0xff && big[21] == 0xfe && big[30] == 1,
          "big-endian motion packet and forced hint zero");

  auto crossing = encode_crossing_event(ByteOrder::LittleEndian, 7,
      {CoreEventType::LeaveNotify, NotifyDetail::Nonlinear, 8, 1, 2, 3,
       4, 5, -6, -7, 0x1234, NotifyMode::Normal, true, true});
  require(crossing.size() == 32 && crossing[0] == 8 && crossing[1] == 3 &&
          crossing[28] == 0x34 && crossing[29] == 0x12 && crossing[30] == 0 &&
          crossing[31] == 3, "crossing encodes state mode same-screen and focus");

  auto focus = encode_focus_event(ByteOrder::BigEndian, 0x12345,
      {CoreEventType::FocusOut, NotifyDetail::Inferior, 0x01020304, NotifyMode::Normal});
  require(focus.size() == 32 && focus[0] == 10 && focus[1] == 2 &&
          focus[2] == 0x23 && focus[3] == 0x45 && focus[4] == 1 &&
          focus[5] == 2 && focus[6] == 3 && focus[7] == 4 &&
          std::all_of(focus.begin() + 8, focus.end(), [](auto b) { return b == 0; }),
          "focus packet fields and padding");
}
