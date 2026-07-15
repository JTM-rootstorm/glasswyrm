#include "input/xkb_keymap.hpp"

#include "helpers/test_support.hpp"

#include <xkbcommon/xkbcommon-keysyms.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

using glasswyrm::input::PreparedKeyTransition;
using glasswyrm::input::XkbKeymap;
using glasswyrm::input::XkbKeymapConfig;
using gw::test::require;
namespace cm = glasswyrm::input::core_modifier_mask;

namespace {

bool contains(const std::span<const std::uint8_t> values,
              const std::uint8_t value) {
  return std::ranges::find(values, value) != values.end();
}

PreparedKeyTransition prepare(XkbKeymap& keymap,
                              const std::uint32_t evdev_keycode,
                              const bool pressed) {
  PreparedKeyTransition transition;
  std::string error;
  require(keymap.prepare_transition(evdev_keycode, pressed, transition, error),
          error);
  return transition;
}

void apply(XkbKeymap& keymap, const PreparedKeyTransition& transition) {
  std::string error;
  require(keymap.apply_transition(transition, error), error);
}

}  // namespace

int main() {
  std::string error;
  auto keymap = XkbKeymap::create(XkbKeymapConfig{}, error);
  require(keymap != nullptr && error.empty(), "default US keymap creation");
  require(keymap->config().rules == "evdev" &&
              keymap->config().model == "pc105" &&
              keymap->config().layout == "us" &&
              keymap->config().variant.empty() &&
              keymap->config().options.empty(),
          "configured RMLVO names are preserved");

  std::uint8_t x11_keycode = 0;
  require(keymap->evdev_to_x11(30, x11_keycode, error) &&
              x11_keycode == 38,
          "evdev keycodes use the X11 plus-eight offset");
  require(!keymap->evdev_to_x11(248, x11_keycode, error) && !error.empty(),
          "out-of-range translated keycode is reported");

  const auto a_keysyms = keymap->core_keysyms(38);
  require(a_keysyms == std::array<std::uint32_t, 4>{
                              XKB_KEY_a, XKB_KEY_A, XKB_KEY_NoSymbol,
                              XKB_KEY_NoSymbol},
          "core mapping has fixed group0/group1 level0/level1 slots");
  require(keymap->core_keysyms(9)[0] == XKB_KEY_Escape,
          "core mapping exposes non-printing keysyms");

  auto shift_down = prepare(*keymap, 42, true);
  require(shift_down.x11_keycode == 50 &&
              shift_down.keysym_before == XKB_KEY_Shift_L &&
              shift_down.core_state_before == 0 && !shift_down.repeatable,
          "modifier press captures pre-transition state and repeatability");
  require(keymap->core_modifier_state() == 0,
          "prepare does not mutate xkb state");
  apply(*keymap, shift_down);
  require((keymap->core_modifier_state() & cm::Shift) != 0,
          "apply updates xkb modifier state after event preparation");

  auto a_down = prepare(*keymap, 30, true);
  require(a_down.keysym_before == XKB_KEY_A &&
              a_down.core_state_before == cm::Shift && a_down.repeatable,
          "effective keysym and core mask use the pre-transition state");
  apply(*keymap, a_down);
  const auto& held = keymap->query_keymap();
  require((held[38 / 8] & (1U << (38 % 8))) != 0 &&
              (held[50 / 8] & (1U << (50 % 8))) != 0,
          "QueryKeymap bytes reflect held X11 keycodes");
  require(keymap->key_repeats(38) && !keymap->key_repeats(50),
          "keymap repeatability distinguishes ordinary and modifier keys");

  PreparedKeyTransition duplicate;
  require(!keymap->prepare_transition(30, true, duplicate, error) &&
              error.find("already held") != std::string::npos,
          "duplicate key press is rejected with an error");
  require(!keymap->apply_transition(shift_down, error) &&
              error.find("stale") != std::string::npos,
          "stale prepared transitions cannot reorder state updates");

  const auto a_up = prepare(*keymap, 30, false);
  require(a_up.keysym_before == XKB_KEY_A &&
              a_up.core_state_before == cm::Shift,
          "release packets also capture state before mutation");
  apply(*keymap, a_up);
  apply(*keymap, prepare(*keymap, 42, false));
  require(keymap->core_modifier_state() == 0 &&
              keymap->query_keymap() == std::array<std::uint8_t, 32>{},
          "releases clear modifier and held-key state");

  const auto modifier_map = keymap->core_modifier_map();
  require(modifier_map.keycodes_per_modifier >= 2 &&
              modifier_map.keycodes.size() ==
                  8U * modifier_map.keycodes_per_modifier &&
              contains(modifier_map.modifier(0), 50) &&
              contains(modifier_map.modifier(0), 62) &&
              contains(modifier_map.modifier(2), 37) &&
              contains(modifier_map.modifier(2), 105) &&
              contains(modifier_map.modifier(3), 64),
          "core modifier map is derived from actual modifier keycodes");
  require(modifier_map.modifier(8).empty(),
          "out-of-range modifier-map access is bounded");

  apply(*keymap, prepare(*keymap, 58, true));
  require((keymap->core_modifier_state() & cm::Lock) != 0,
          "lock state is serialized into the core mask");
  apply(*keymap, prepare(*keymap, 30, true));
  require(keymap->reset_on_suspend(error) && error.empty() &&
              keymap->core_modifier_state() == 0 &&
              keymap->query_keymap() == std::array<std::uint8_t, 32>{},
          "suspend reset clears locked modifiers and held keys");

  XkbKeymapConfig narrow;
  narrow.maximum_x11_keycode = 37;
  auto narrow_keymap = XkbKeymap::create(narrow, error);
  require(narrow_keymap != nullptr &&
              !narrow_keymap->evdev_to_x11(30, x11_keycode, error) &&
              error.find("setup range") != std::string::npos,
          "setup keycode range is enforced");

  XkbKeymapConfig invalid;
  invalid.layout = "glasswyrm_layout_that_does_not_exist";
  require(XkbKeymap::create(invalid, error) == nullptr &&
              error.find("failed to compile") != std::string::npos &&
              error.find(invalid.layout) != std::string::npos,
          "keymap compilation reports the failing configuration");
}
