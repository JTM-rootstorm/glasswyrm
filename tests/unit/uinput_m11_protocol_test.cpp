#include "tests/helpers/uinput_m11_protocol.hpp"

#include <linux/input-event-codes.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <iterator>
#include <set>
#include <string_view>

namespace protocol = gw::test::uinput_m11;

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) std::fprintf(stderr, "FAIL: %.*s\n",
                               static_cast<int>(message.size()), message.data());
  return condition;
}

bool balanced(const std::vector<protocol::Event> &events) {
  std::set<std::pair<protocol::Device, std::uint16_t>> pressed;
  for (const auto &event : events) {
    if (event.type != EV_KEY) continue;
    const auto identity = std::pair(event.device, event.code);
    if (event.value == 1) {
      if (!pressed.insert(identity).second) return false;
    } else if (event.value == 0) {
      if (pressed.erase(identity) != 1) return false;
    } else {
      return false;
    }
  }
  return pressed.empty();
}

bool is_event(const protocol::Event &event, protocol::Device device,
              std::uint16_t type, std::uint16_t code, std::int32_t value) {
  return event.device == device && event.type == type && event.code == code &&
         event.value == value;
}

}  // namespace

int main() {
  constexpr std::array<std::string_view, 10> expected{
      "basic-typing", "repeat",         "scroll", "primary-selection",
      "clipboard-probe", "move",       "resize", "close",
      "post-vt",      "post-restart",
  };
  bool okay = true;
  const auto &names = protocol::scenario_names();
  okay &= expect(std::equal(names.begin(), names.end(), expected.begin(),
                            expected.end()),
                 "scenario allowlist is exact and ordered");

  for (const auto name : expected) {
    std::string_view parsed;
    const auto request = protocol::encode_request(name);
    okay &= expect(request == "run " + std::string(name),
                   "request encoding is canonical");
    okay &= expect(protocol::parse_request(request, parsed) && parsed == name,
                   "canonical request round trips");
    const auto events = protocol::scenario_events(name);
    okay &= expect(!events.empty(), "scenario has a fixed event sequence");
    okay &= expect(events.size() <= 160, "scenario sequence remains bounded");
    okay &= expect(balanced(events), "scenario releases every pressed key");
  }

  for (const auto invalid : {
           "", "run", "run unknown", "run basic-typing\n",
           "run basic-typing extra", "run basic-typing;id", "basic-typing",
       }) {
    std::string_view parsed;
    okay &= expect(!protocol::parse_request(invalid, parsed),
                   "noncanonical request is rejected");
  }
  okay &= expect(protocol::encode_request("unknown").empty(),
                 "unknown scenario cannot be encoded");

  const auto scroll = protocol::scenario_events("scroll");
  const auto pointer_start = std::ranges::find_if(
      scroll, [](const protocol::Event &event) {
        return is_event(event, protocol::Device::pointer, EV_REL, REL_X, 120);
      });
  okay &= expect(pointer_start != scroll.end() &&
                     std::distance(pointer_start, scroll.end()) >= 4 &&
                     is_event(*std::next(pointer_start),
                              protocol::Device::pointer, EV_REL, REL_Y, 128) &&
                     is_event(*std::next(pointer_start, 2),
                              protocol::Device::pointer, EV_REL, REL_WHEEL,
                              -4) &&
                     is_event(*std::next(pointer_start, 3),
                              protocol::Device::pointer, EV_REL, REL_WHEEL, 4),
                 "scroll creates scrollback, enters xterm A, and restores its view");
  const auto primary = protocol::scenario_events("primary-selection");
  okay &= expect(primary.size() == 5 &&
                     is_event(primary[0], protocol::Device::pointer, EV_REL,
                              REL_Y, -13) &&
                     is_event(primary[1], protocol::Device::pointer, EV_REL,
                              REL_X, -6) &&
                     is_event(primary[2], protocol::Device::pointer, EV_KEY,
                              BTN_LEFT, 1) &&
                     is_event(primary[3], protocol::Device::pointer, EV_REL,
                              REL_X, 111) &&
                     is_event(primary[4], protocol::Device::pointer, EV_KEY,
                              BTN_LEFT, 0) && primary[4].delay_ms == 250,
                 "PRIMARY drag selects the known xterm A output row");
  const auto paste = protocol::scenario_events("clipboard-probe");
  okay &= expect(paste.size() > 8 &&
                     is_event(paste[0], protocol::Device::pointer, EV_REL,
                              REL_X, 335) &&
                     is_event(paste[1], protocol::Device::pointer, EV_REL,
                              REL_Y, 98) &&
                     is_event(paste[2], protocol::Device::pointer, EV_KEY,
                              BTN_MIDDLE, 1) &&
                     is_event(paste[3], protocol::Device::pointer, EV_KEY,
                              BTN_MIDDLE, 0) && paste[3].delay_ms == 250 &&
                     is_event(paste[4], protocol::Device::keyboard, EV_KEY,
                              KEY_LEFTCTRL, 1),
                 "PRIMARY paste targets and focuses xterm B before editing");
  const auto close = protocol::scenario_events("close");
  okay &= expect(close.size() == 8 &&
                     is_event(close[0], protocol::Device::pointer, EV_REL,
                              REL_X, -608) &&
                     is_event(close[1], protocol::Device::pointer, EV_REL,
                              REL_Y, -205) &&
                     is_event(close[2], protocol::Device::pointer, EV_KEY,
                              BTN_LEFT, 1) &&
                     is_event(close[3], protocol::Device::pointer, EV_KEY,
                              BTN_LEFT, 0) && close[3].delay_ms == 250 &&
                     is_event(close[4], protocol::Device::keyboard, EV_KEY,
                              KEY_LEFTALT, 1) &&
                     is_event(close[7], protocol::Device::keyboard, EV_KEY,
                              KEY_LEFTALT, 0),
                 "close refocuses xterm A before Alt-F4");

  const auto keys = protocol::keyboard_key_codes();
  for (const auto required : {KEY_A, KEY_BACKSPACE, KEY_ENTER, KEY_F4,
                              KEY_LEFTALT, KEY_LEFTCTRL, KEY_LEFTSHIFT,
                              KEY_L}) {
    okay &= expect(std::binary_search(keys.begin(), keys.end(), required),
                   "keyboard capability contains a scenario key");
  }

  protocol::DeviceIdentity keyboard{
      std::string(protocol::kKeyboardName), "input7", "/dev/input/event7",
      6, 0x4757, 0x1101, 1, false};
  protocol::DeviceIdentity pointer{
      std::string(protocol::kPointerName), "input8", "/dev/input/event8",
      6, 0x4757, 0x1102, 1, true};
  const auto json = protocol::devices_json(keyboard, pointer);
  okay &= expect(
      json ==
          "{\"schema\":1,\"keyboard\":{\"name\":\"Glasswyrm M11 Test "
          "Keyboard\",\"sysname\":\"input7\",\"event_path\":\"/dev/input/"
          "event7\",\"bustype\":6,\"vendor\":18263,\"product\":4353,"
          "\"version\":1,\"horizontal_wheel\":false},\"pointer\":{\"name\":"
          "\"Glasswyrm M11 Test Pointer\",\"sysname\":\"input8\","
          "\"event_path\":\"/dev/input/event8\",\"bustype\":6,\"vendor\":"
          "18263,\"product\":4354,\"version\":1,\"horizontal_wheel\":"
          "true}}\n",
      "device JSON is deterministic");
  okay &= expect(protocol::result_json("move", "completed", 6) ==
                     "{\"schema\":1,\"scenario\":\"move\",\"status\":"
                     "\"completed\",\"event_count\":6}\n",
                 "result JSON is deterministic");
  return okay ? 0 : 1;
}
