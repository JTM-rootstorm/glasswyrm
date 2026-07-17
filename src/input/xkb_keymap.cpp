#include "input/xkb_keymap.hpp"

#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <array>
#include <limits>
#include <sstream>
#include <utility>

namespace glasswyrm::input {
namespace {

constexpr std::uint32_t kX11KeycodeOffset = 8;
constexpr std::array<const char*, 8> kCoreModifierNames{
    "Shift", "Lock", "Control", "Mod1", "Mod2", "Mod3", "Mod4", "Mod5"};
constexpr std::array<std::uint16_t, 8> kCoreModifierMasks{
    core_modifier_mask::Shift, core_modifier_mask::Lock,
    core_modifier_mask::Control, core_modifier_mask::Mod1,
    core_modifier_mask::Mod2, core_modifier_mask::Mod3,
    core_modifier_mask::Mod4, core_modifier_mask::Mod5};

std::string describe_config(const XkbKeymapConfig& config) {
  std::ostringstream stream;
  stream << "rules='" << config.rules << "', model='" << config.model
         << "', layout='" << config.layout << "', variant='"
         << config.variant << "', options='" << config.options << "'";
  return stream.str();
}

}  // namespace

std::span<const std::uint8_t> CoreModifierMap::modifier(
    const std::size_t index) const noexcept {
  if (index >= kCoreModifierNames.size() || keycodes_per_modifier == 0)
    return {};
  const auto offset = index * keycodes_per_modifier;
  if (offset + keycodes_per_modifier > keycodes.size()) return {};
  return std::span<const std::uint8_t>(keycodes).subspan(
      offset, keycodes_per_modifier);
}

std::unique_ptr<XkbKeymap> XkbKeymap::create(XkbKeymapConfig config,
                                             std::string& error) {
  error.clear();
  if (config.minimum_x11_keycode > config.maximum_x11_keycode) {
    error = "minimum X11 keycode exceeds maximum X11 keycode";
    return nullptr;
  }

  auto* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (context == nullptr) {
    error = "failed to create libxkbcommon context";
    return nullptr;
  }

  const xkb_rule_names names{config.rules.c_str(), config.model.c_str(),
                             config.layout.c_str(), config.variant.c_str(),
                             config.options.c_str()};
  auto* keymap = xkb_keymap_new_from_names(context, &names,
                                           XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (keymap == nullptr) {
    error = "failed to compile libxkbcommon keymap (" +
            describe_config(config) + ")";
    xkb_context_unref(context);
    return nullptr;
  }

  auto* state = xkb_state_new(keymap);
  if (state == nullptr) {
    error = "failed to create libxkbcommon state";
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    return nullptr;
  }
  return std::unique_ptr<XkbKeymap>(
      new XkbKeymap(std::move(config), context, keymap, state));
}

XkbKeymap::XkbKeymap(XkbKeymapConfig config, xkb_context* context,
                     xkb_keymap* keymap, xkb_state* state) noexcept
    : config_(std::move(config)),
      context_(context),
      keymap_(keymap),
      state_(state) {}

XkbKeymap::~XkbKeymap() {
  xkb_state_unref(state_);
  xkb_keymap_unref(keymap_);
  xkb_context_unref(context_);
}

bool XkbKeymap::evdev_to_x11(const std::uint32_t evdev_keycode,
                             std::uint8_t& x11_keycode,
                             std::string& error) const {
  error.clear();
  if (evdev_keycode >
      std::numeric_limits<std::uint8_t>::max() - kX11KeycodeOffset) {
    error = "evdev keycode cannot be represented as a core X11 keycode";
    return false;
  }
  const auto translated = evdev_keycode + kX11KeycodeOffset;
  if (translated < config_.minimum_x11_keycode ||
      translated > config_.maximum_x11_keycode) {
    error = "translated X11 keycode is outside the configured setup range";
    return false;
  }
  x11_keycode = static_cast<std::uint8_t>(translated);
  return true;
}

bool XkbKeymap::prepare_transition(
    const std::uint32_t evdev_keycode, const bool pressed,
    PreparedKeyTransition& transition, std::string& error) const {
  std::uint8_t x11_keycode = 0;
  if (!evdev_to_x11(evdev_keycode, x11_keycode, error)) return false;
  if (keycode_is_held(x11_keycode) == pressed) {
    error = pressed ? "key is already held" : "key is not held";
    return false;
  }

  transition = PreparedKeyTransition{
      evdev_keycode,
      x11_keycode,
      pressed,
      core_modifier_state(),
      static_cast<std::uint32_t>(xkb_state_key_get_one_sym(state_, x11_keycode)),
      key_repeats(x11_keycode),
      revision_};
  error.clear();
  return true;
}

bool XkbKeymap::apply_transition(const PreparedKeyTransition& transition,
                                 std::string& error) {
  error.clear();
  if (transition.revision != revision_) {
    error = "prepared key transition is stale";
    return false;
  }
  std::uint8_t translated = 0;
  if (!evdev_to_x11(transition.evdev_keycode, translated, error)) return false;
  if (translated != transition.x11_keycode) {
    error = "prepared key transition has an inconsistent X11 keycode";
    return false;
  }
  if (keycode_is_held(translated) == transition.pressed) {
    error = transition.pressed ? "key is already held" : "key is not held";
    return false;
  }

  xkb_state_update_key(state_, translated,
                       transition.pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
  set_keycode_held(translated, transition.pressed);
  ++revision_;
  return true;
}

std::uint16_t XkbKeymap::core_modifier_state() const noexcept {
  std::uint16_t result = 0;
  for (std::size_t index = 0; index < kCoreModifierNames.size(); ++index) {
    if (xkb_state_mod_name_is_active(state_, kCoreModifierNames[index],
                                     XKB_STATE_MODS_EFFECTIVE) > 0)
      result |= kCoreModifierMasks[index];
  }
  return result;
}

std::array<std::uint32_t, XkbKeymap::keysyms_per_keycode>
XkbKeymap::core_keysyms(const std::uint8_t x11_keycode) const noexcept {
  std::array<std::uint32_t, keysyms_per_keycode> result{};
  const auto layout_count =
      xkb_keymap_num_layouts_for_key(keymap_, x11_keycode);
  for (xkb_layout_index_t layout = 0; layout < 2 && layout < layout_count;
       ++layout) {
    const auto level_count =
        xkb_keymap_num_levels_for_key(keymap_, x11_keycode, layout);
    for (xkb_level_index_t level = 0; level < 2 && level < level_count;
         ++level) {
      const xkb_keysym_t* keysyms = nullptr;
      const auto count = xkb_keymap_key_get_syms_by_level(
          keymap_, x11_keycode, layout, level, &keysyms);
      if (count > 0 && keysyms != nullptr)
        result[(layout * 2U) + level] = keysyms[0];
    }
  }
  return result;
}

CoreModifierMap XkbKeymap::core_modifier_map() const {
  std::array<std::vector<std::uint8_t>, 8> modifiers;
  const auto keymap_min = xkb_keymap_min_keycode(keymap_);
  const auto keymap_max = xkb_keymap_max_keycode(keymap_);
  const auto first = std::max<std::uint32_t>(config_.minimum_x11_keycode,
                                             keymap_min);
  const auto last = std::min<std::uint32_t>(config_.maximum_x11_keycode,
                                            keymap_max);

  for (auto keycode = first; keycode <= last; ++keycode) {
    auto* state = xkb_state_new(keymap_);
    if (state == nullptr) continue;
    xkb_state_update_key(state, keycode, XKB_KEY_DOWN);
    for (std::size_t index = 0; index < kCoreModifierNames.size(); ++index) {
      if (xkb_state_mod_name_is_active(state, kCoreModifierNames[index],
                                       XKB_STATE_MODS_EFFECTIVE) > 0)
        modifiers[index].push_back(static_cast<std::uint8_t>(keycode));
    }
    xkb_state_unref(state);
    if (keycode == last) break;
  }

  std::size_t width = 0;
  for (const auto& modifier : modifiers)
    width = std::max(width, modifier.size());
  CoreModifierMap result;
  result.keycodes_per_modifier = static_cast<std::uint8_t>(width);
  result.keycodes.assign(kCoreModifierNames.size() * width, 0);
  for (std::size_t index = 0; index < modifiers.size(); ++index)
    std::ranges::copy(modifiers[index],
                      std::span<std::uint8_t>(result.keycodes)
                          .subspan(index * width)
                          .begin());
  return result;
}

bool XkbKeymap::key_repeats(const std::uint8_t x11_keycode) const noexcept {
  return xkb_keymap_key_repeats(keymap_, x11_keycode) != 0;
}

bool XkbKeymap::reset_on_suspend(std::string& error) {
  auto* reset_state = xkb_state_new(keymap_);
  if (reset_state == nullptr) {
    error = "failed to reset libxkbcommon state during input suspension";
    return false;
  }
  xkb_state_unref(state_);
  state_ = reset_state;
  held_keys_.fill(0);
  ++revision_;
  error.clear();
  return true;
}

bool XkbKeymap::keycode_is_held(const std::uint8_t keycode) const noexcept {
  const auto byte = static_cast<std::size_t>(keycode / 8U);
  const auto bit = static_cast<std::uint8_t>(1U << (keycode % 8U));
  return (held_keys_[byte] & bit) != 0;
}

void XkbKeymap::set_keycode_held(const std::uint8_t keycode,
                                 const bool held) noexcept {
  const auto byte = static_cast<std::size_t>(keycode / 8U);
  const auto bit = static_cast<std::uint8_t>(1U << (keycode % 8U));
  if (held)
    held_keys_[byte] |= bit;
  else
    held_keys_[byte] &= static_cast<std::uint8_t>(~bit);
}

}  // namespace glasswyrm::input
