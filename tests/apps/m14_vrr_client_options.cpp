#include "m14_vrr_client_options.hpp"

#include <charconv>
#include <string_view>

namespace gw::test::m14 {
namespace {

bool parse_u32(const std::string_view text, std::uint32_t &value) noexcept {
  if (text.empty())
    return false;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parse_mode(const std::string_view text, ClientMode &mode) noexcept {
  if (text == "fullscreen")
    mode = ClientMode::Fullscreen;
  else if (text == "borderless")
    mode = ClientMode::Borderless;
  else if (text == "windowed")
    mode = ClientMode::Windowed;
  else if (text == "app-requested")
    mode = ClientMode::AppRequested;
  else if (text == "cadence")
    mode = ClientMode::Cadence;
  else
    return false;
  return true;
}

} // namespace

bool parse_client_options(const int argc, char **argv, ClientOptions &options) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--help") {
      options.help = true;
    } else if (argument == "--self-test") {
      options.self_test = true;
    } else if (argument == "--prefer") {
      options.prefer = true;
    } else if (argument == "--display" && index + 1 < argc) {
      options.display = argv[++index];
    } else if (argument == "--result" && index + 1 < argc) {
      options.result_path = argv[++index];
    } else if (argument == "--mode" && index + 1 < argc) {
      if (!parse_mode(argv[++index], options.mode))
        return false;
      options.mode_set = true;
    } else if (argument == "--frames" && index + 1 < argc) {
      if (!parse_u32(argv[++index], options.frame_count) ||
          options.frame_count == 0 || options.frame_count > 10'000)
        return false;
      options.frame_count_set = true;
    } else if (argument == "--target-refresh-hz" && index + 1 < argc) {
      if (!parse_u32(argv[++index], options.target_refresh_hz) ||
          options.target_refresh_hz == 0 || options.target_refresh_hz > 1'000)
        return false;
    } else if (argument == "--hold-ms" && index + 1 < argc) {
      if (!parse_u32(argv[++index], options.hold_ms) ||
          options.hold_ms > 60'000)
        return false;
    } else {
      return false;
    }
  }

  if (options.mode == ClientMode::Cadence && !options.frame_count_set)
    options.frame_count = 180;
  if (options.mode != ClientMode::Cadence && options.frame_count_set &&
      options.frame_count != 1)
    return false;
  if (options.mode == ClientMode::AppRequested)
    options.prefer = true;
  if (options.help || options.self_test)
    return true;
  return options.mode_set && !options.display.empty() &&
         !options.result_path.empty();
}

std::string_view client_mode_name(const ClientMode mode) noexcept {
  switch (mode) {
  case ClientMode::Fullscreen:
    return "fullscreen";
  case ClientMode::Borderless:
    return "borderless";
  case ClientMode::Windowed:
    return "windowed";
  case ClientMode::AppRequested:
    return "app-requested";
  case ClientMode::Cadence:
    return "cadence";
  }
  return {};
}

} // namespace gw::test::m14
