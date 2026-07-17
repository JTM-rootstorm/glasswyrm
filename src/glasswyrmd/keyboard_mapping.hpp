#pragma once

#include <cstdint>
#include <vector>

namespace glasswyrm::server {

struct KeyboardMappingSnapshot {
  std::uint8_t minimum_keycode{8};
  std::uint8_t maximum_keycode{255};
  std::uint8_t keysyms_per_keycode{};
  std::vector<std::uint32_t> keysyms;
  std::uint8_t keycodes_per_modifier{};
  std::vector<std::uint8_t> modifier_keycodes;
};

}  // namespace glasswyrm::server
