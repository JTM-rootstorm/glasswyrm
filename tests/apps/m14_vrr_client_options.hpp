#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace gw::test::m14 {

enum class ClientMode : std::uint8_t {
  Fullscreen,
  Borderless,
  Windowed,
  AppRequested,
  Cadence,
};

inline constexpr std::string_view kClientUsage =
    "Usage: m14_vrr_client --display :N --mode "
    "fullscreen|borderless|windowed|app-requested|cadence --result PATH "
    "[--frames 1..10000] [--target-refresh-hz 1..1000] "
    "[--hold-ms 0..60000] [--prefer]\n"
    "       m14_vrr_client --self-test\n";

struct ClientOptions {
  std::string display;
  std::string result_path;
  ClientMode mode{ClientMode::Windowed};
  std::uint32_t frame_count{1};
  std::uint32_t target_refresh_hz{72};
  std::uint32_t hold_ms{250};
  bool mode_set{};
  bool frame_count_set{};
  bool prefer{};
  bool self_test{};
  bool help{};
};

[[nodiscard]] bool parse_client_options(int argc, char **argv,
                                        ClientOptions &options);
[[nodiscard]] std::string_view client_mode_name(ClientMode mode) noexcept;

} // namespace gw::test::m14
