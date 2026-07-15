#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gw::test::uinput_m11 {

inline constexpr std::string_view kKeyboardName =
    "Glasswyrm M11 Test Keyboard";
inline constexpr std::string_view kPointerName =
    "Glasswyrm M11 Test Pointer";

enum class Device { keyboard, pointer };

struct Event {
  Device device{};
  std::uint16_t type{};
  std::uint16_t code{};
  std::int32_t value{};
  std::uint16_t delay_ms{};
};

struct DeviceIdentity {
  std::string name;
  std::string sysname;
  std::string event_path;
  std::uint16_t bustype{};
  std::uint16_t vendor{};
  std::uint16_t product{};
  std::uint16_t version{};
  bool horizontal_wheel{};
};

const std::vector<std::string_view> &scenario_names();
bool known_scenario(std::string_view name);
std::vector<Event> scenario_events(std::string_view name);
std::vector<std::uint16_t> keyboard_key_codes();

std::string encode_request(std::string_view scenario);
bool parse_request(std::string_view packet, std::string_view &scenario);
std::string devices_json(const DeviceIdentity &keyboard,
                         const DeviceIdentity &pointer);
std::string result_json(std::string_view scenario, std::string_view status,
                        std::size_t event_count);

}  // namespace gw::test::uinput_m11
