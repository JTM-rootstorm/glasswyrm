#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

struct xkb_context;
struct xkb_keymap;
struct xkb_state;

namespace glasswyrm::input {

namespace core_modifier_mask {
inline constexpr std::uint16_t Shift = 1U << 0U;
inline constexpr std::uint16_t Lock = 1U << 1U;
inline constexpr std::uint16_t Control = 1U << 2U;
inline constexpr std::uint16_t Mod1 = 1U << 3U;
inline constexpr std::uint16_t Mod2 = 1U << 4U;
inline constexpr std::uint16_t Mod3 = 1U << 5U;
inline constexpr std::uint16_t Mod4 = 1U << 6U;
inline constexpr std::uint16_t Mod5 = 1U << 7U;
}  // namespace core_modifier_mask

struct XkbKeymapConfig {
  std::string rules{"evdev"};
  std::string model{"pc105"};
  std::string layout{"us"};
  std::string variant;
  std::string options;
  std::uint8_t minimum_x11_keycode{8};
  std::uint8_t maximum_x11_keycode{255};
};

struct PreparedKeyTransition {
  std::uint32_t evdev_keycode{0};
  std::uint8_t x11_keycode{0};
  bool pressed{false};
  std::uint16_t core_state_before{0};
  std::uint32_t keysym_before{0};
  bool repeatable{false};
  std::uint64_t revision{0};
};

struct CoreModifierMap {
  std::uint8_t keycodes_per_modifier{0};
  std::vector<std::uint8_t> keycodes;

  [[nodiscard]] std::span<const std::uint8_t> modifier(
      std::size_t index) const noexcept;
};

class XkbKeymap {
 public:
  static constexpr std::size_t keysyms_per_keycode = 4;

  [[nodiscard]] static std::unique_ptr<XkbKeymap> create(
      XkbKeymapConfig config, std::string& error);

  ~XkbKeymap();
  XkbKeymap(const XkbKeymap&) = delete;
  XkbKeymap& operator=(const XkbKeymap&) = delete;

  [[nodiscard]] const XkbKeymapConfig& config() const noexcept {
    return config_;
  }
  [[nodiscard]] bool evdev_to_x11(std::uint32_t evdev_keycode,
                                  std::uint8_t& x11_keycode,
                                  std::string& error) const;
  [[nodiscard]] bool prepare_transition(std::uint32_t evdev_keycode,
                                        bool pressed,
                                        PreparedKeyTransition& transition,
                                        std::string& error) const;
  [[nodiscard]] bool apply_transition(
      const PreparedKeyTransition& transition, std::string& error);

  [[nodiscard]] std::uint16_t core_modifier_state() const noexcept;
  [[nodiscard]] std::array<std::uint32_t, keysyms_per_keycode>
  core_keysyms(std::uint8_t x11_keycode) const noexcept;
  [[nodiscard]] CoreModifierMap core_modifier_map() const;
  [[nodiscard]] const std::array<std::uint8_t, 32>& query_keymap()
      const noexcept {
    return held_keys_;
  }
  [[nodiscard]] bool key_repeats(std::uint8_t x11_keycode) const noexcept;
  [[nodiscard]] bool reset_on_suspend(std::string& error);

 private:
  XkbKeymap(XkbKeymapConfig config, xkb_context* context,
            xkb_keymap* keymap, xkb_state* state) noexcept;

  [[nodiscard]] bool keycode_is_held(std::uint8_t keycode) const noexcept;
  void set_keycode_held(std::uint8_t keycode, bool held) noexcept;

  XkbKeymapConfig config_;
  xkb_context* context_{nullptr};
  xkb_keymap* keymap_{nullptr};
  xkb_state* state_{nullptr};
  std::array<std::uint8_t, 32> held_keys_{};
  std::uint64_t revision_{0};
};

}  // namespace glasswyrm::input
