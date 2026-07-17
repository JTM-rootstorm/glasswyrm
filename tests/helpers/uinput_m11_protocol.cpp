#include "tests/helpers/uinput_m11_protocol.hpp"

#include <linux/input-event-codes.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <utility>

namespace gw::test::uinput_m11 {
namespace {

using Events = std::vector<Event>;

constexpr std::array<std::string_view, 10> kScenarios{
    "basic-typing", "repeat",         "scroll", "primary-selection",
    "clipboard-probe", "move",       "resize", "close",
    "post-vt",      "post-restart",
};

void add(Events &events, Device device, std::uint16_t type,
         std::uint16_t code, std::int32_t value,
         std::uint16_t delay_ms = 8) {
  events.push_back({device, type, code, value, delay_ms});
}

void key(Events &events, std::uint16_t code, bool pressed,
         std::uint16_t delay_ms = 8) {
  add(events, Device::keyboard, EV_KEY, code, pressed ? 1 : 0, delay_ms);
}

void tap(Events &events, std::uint16_t code) {
  key(events, code, true);
  key(events, code, false);
}

void chord(Events &events, std::uint16_t modifier, std::uint16_t code) {
  key(events, modifier, true);
  tap(events, code);
  key(events, modifier, false);
}

struct KeyStroke {
  std::uint16_t code;
  bool shift;
};

bool key_stroke(char character, KeyStroke &stroke) {
  static const std::map<char, KeyStroke> keys{
      {' ', {KEY_SPACE, false}},       {'\'', {KEY_APOSTROPHE, false}},
      {'%', {KEY_5, true}},            {'\\', {KEY_BACKSLASH, false}},
      {'_', {KEY_MINUS, true}},        {'1', {KEY_1, false}},
      {'0', {KEY_0, false}},           {'4', {KEY_4, false}},
      {'a', {KEY_A, false}},           {'d', {KEY_D, false}},
      {'e', {KEY_E, false}},           {'f', {KEY_F, false}},
      {'i', {KEY_I, false}},           {'n', {KEY_N, false}},
      {'p', {KEY_P, false}},           {'r', {KEY_R, false}},
      {'q', {KEY_Q, false}},           {'s', {KEY_S, false}},
      {'t', {KEY_T, false}},
      {'y', {KEY_Y, false}},           {'A', {KEY_A, true}},
      {'C', {KEY_C, true}},            {'D', {KEY_D, true}},
      {'E', {KEY_E, true}},            {'I', {KEY_I, true}},
      {'K', {KEY_K, true}},            {'L', {KEY_L, true}},
      {'M', {KEY_M, true}},            {'P', {KEY_P, true}},
      {'N', {KEY_N, true}},            {'O', {KEY_O, true}},
      {'R', {KEY_R, true}},            {'S', {KEY_S, true}},
      {'T', {KEY_T, true}},            {'V', {KEY_V, true}},
      {'X', {KEY_X, true}},            {'Y', {KEY_Y, true}},
  };
  const auto found = keys.find(character);
  if (found == keys.end()) return false;
  stroke = found->second;
  return true;
}

void text(Events &events, std::string_view value) {
  for (const char character : value) {
    KeyStroke stroke{};
    if (!key_stroke(character, stroke)) std::abort();
    if (stroke.shift) key(events, KEY_LEFTSHIFT, true);
    tap(events, stroke.code);
    if (stroke.shift) key(events, KEY_LEFTSHIFT, false);
  }
}

void relative(Events &events, std::uint16_t code, std::int32_t value) {
  add(events, Device::pointer, EV_REL, code, value);
}

void button(Events &events, std::uint16_t code, bool pressed,
            std::uint16_t delay_ms = 8) {
  add(events, Device::pointer, EV_KEY, code, pressed ? 1 : 0, delay_ms);
}

std::string escape_json(std::string_view value) {
  std::string result;
  result.reserve(value.size() + 8);
  for (const unsigned char character : value) {
    switch (character) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\b': result += "\\b"; break;
      case '\f': result += "\\f"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (character < 0x20) {
          char encoded[7]{};
          std::snprintf(encoded, sizeof(encoded), "\\u%04x", character);
          result += encoded;
        } else {
          result += static_cast<char>(character);
        }
    }
  }
  return result;
}

void append_identity(std::string &json, const DeviceIdentity &identity) {
  json += "{\"name\":\"" + escape_json(identity.name) + "\"";
  json += ",\"sysname\":\"" + escape_json(identity.sysname) + "\"";
  json += ",\"event_path\":\"" + escape_json(identity.event_path) + "\"";
  json += ",\"bustype\":" + std::to_string(identity.bustype);
  json += ",\"vendor\":" + std::to_string(identity.vendor);
  json += ",\"product\":" + std::to_string(identity.product);
  json += ",\"version\":" + std::to_string(identity.version);
  json += ",\"horizontal_wheel\":";
  json += identity.horizontal_wheel ? "true}" : "false}";
}

}  // namespace

const std::vector<std::string_view> &scenario_names() {
  static const std::vector<std::string_view> names(kScenarios.begin(),
                                                   kScenarios.end());
  return names;
}

bool known_scenario(std::string_view name) {
  return std::find(kScenarios.begin(), kScenarios.end(), name) !=
         kScenarios.end();
}

std::vector<Event> scenario_events(std::string_view name) {
  Events events;
  if (name == "basic-typing") {
    text(events, "printf 'M11_TYPEX");
    tap(events, KEY_BACKSPACE);
    text(events, "D\\n'");
    tap(events, KEY_ENTER);
    // Repeat the corrected command explicitly. The fixed shell profile keeps
    // history disabled, so KEY_UP cannot provide deterministic re-execution.
    text(events, "printf 'M11_TYPEX");
    tap(events, KEY_BACKSPACE);
    text(events, "D\\n'");
    tap(events, KEY_ENTER);
  } else if (name == "repeat") {
    key(events, KEY_A, true, 700);
    key(events, KEY_A, false);
    chord(events, KEY_LEFTCTRL, KEY_U);
  } else if (name == "scroll") {
    text(events, "seq 40");
    tap(events, KEY_ENTER);
    // Enter xterm A at a stable cell before delivering wheel detents.  Its
    // fixed geometry is 80x24+96+96 and the input state begins at (0, 0).
    relative(events, REL_X, 120);
    relative(events, REL_Y, 128);
    relative(events, REL_WHEEL, -4);
    relative(events, REL_WHEEL, 4);
#ifdef REL_HWHEEL
    relative(events, REL_HWHEEL, 1);
    relative(events, REL_HWHEEL, -1);
#endif
    // Clear the viewport before printing the selection token so it occupies a
    // stable row independent of the amount of scrollback above it.
    chord(events, KEY_LEFTCTRL, KEY_L);
    text(events, "printf 'M11_SELECTION_TOKEN\\n'");
    tap(events, KEY_ENTER);
  } else if (name == "primary-selection") {
    // Select the M11_SELECTION_TOKEN output from xterm A.  The preceding
    // clear leaves the token one fixed bitmap-font row above the prompt. A
    // double click selects the underscore-delimited word without its newline.
    relative(events, REL_Y, -13);
    button(events, BTN_LEFT, true);
    button(events, BTN_LEFT, false);
    button(events, BTN_LEFT, true);
    button(events, BTN_LEFT, false, 250);
  } else if (name == "clipboard-probe") {
    // Move from xterm A to the non-overlapping interior of xterm B
    // (80x24+480+160).  Middle-click both focuses B and inserts PRIMARY.
    relative(events, REL_X, 440);
    relative(events, REL_Y, 98);
    button(events, BTN_MIDDLE, true);
    button(events, BTN_MIDDLE, false, 250);
    // Prefix the pasted token without replacing it, then print it through
    // the PTY so the acceptance transcript proves the cross-client paste.
    chord(events, KEY_LEFTCTRL, KEY_A);
    text(events, "printf 'M11_PASTED_%s\\n' ");
    tap(events, KEY_ENTER);
  } else if (name == "move" || name == "resize") {
    key(events, KEY_LEFTALT, true);
    button(events, name == "move" ? BTN_LEFT : BTN_RIGHT, true);
    relative(events, REL_X, name == "move" ? 96 : 72);
    relative(events, REL_Y, name == "move" ? 64 : 48);
    button(events, name == "move" ? BTN_LEFT : BTN_RIGHT, false);
    // The server keeps the interaction active until both its final geometry
    // transaction and cursor publication are accepted.  Give that asynchronous
    // boundary time to settle before the controller starts the next scenario.
    key(events, KEY_LEFTALT, false, 250);
  } else if (name == "close") {
    // Move from the post-resize pointer location back into xterm A, focus it,
    // and close A while xterm B remains alive.
    relative(events, REL_X, -608);
    relative(events, REL_Y, -205);
    button(events, BTN_LEFT, true);
    button(events, BTN_LEFT, false, 250);
    chord(events, KEY_LEFTALT, KEY_F4);
  } else if (name == "post-vt") {
    text(events, "printf 'M11_VT\\n'");
    tap(events, KEY_ENTER);
  } else if (name == "post-restart") {
    text(events, "printf 'M11_RESTART\\n'");
    tap(events, KEY_ENTER);
  }
  return events;
}

std::vector<std::uint16_t> keyboard_key_codes() {
  std::vector<std::uint16_t> result;
  for (const auto name : kScenarios) {
    for (const auto &event : scenario_events(name)) {
      if (event.device == Device::keyboard && event.type == EV_KEY)
        result.push_back(event.code);
    }
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

std::string encode_request(std::string_view scenario) {
  if (!known_scenario(scenario)) return {};
  return "run " + std::string(scenario);
}

bool parse_request(std::string_view packet, std::string_view &scenario) {
  constexpr std::string_view prefix = "run ";
  if (!packet.starts_with(prefix)) return false;
  const auto candidate = packet.substr(prefix.size());
  if (!known_scenario(candidate)) return false;
  scenario = candidate;
  return true;
}

std::string devices_json(const DeviceIdentity &keyboard,
                         const DeviceIdentity &pointer) {
  std::string result = "{\"schema\":1,\"keyboard\":";
  append_identity(result, keyboard);
  result += ",\"pointer\":";
  append_identity(result, pointer);
  result += "}\n";
  return result;
}

std::string result_json(std::string_view scenario, std::string_view status,
                        std::size_t event_count) {
  return "{\"schema\":1,\"scenario\":\"" + escape_json(scenario) +
         "\",\"status\":\"" + escape_json(status) +
         "\",\"event_count\":" + std::to_string(event_count) + "}\n";
}

}  // namespace gw::test::uinput_m11
