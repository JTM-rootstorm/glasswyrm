#include "backends/drm/connector_name.hpp"
#include "backends/drm/resources.hpp"

#include "tests/helpers/test_support.hpp"

#include <array>
#include <string_view>

int main() {
  constexpr std::array<std::string_view, 21> expected{
      "",          "VGA",       "DVI-I",     "DVI-D", "DVI-A",
      "Composite", "SVIDEO",    "LVDS",      "Component", "DIN",
      "DP",        "HDMI-A",    "HDMI-B",    "TV", "eDP",
      "Virtual",   "DSI",       "DPI",       "Writeback", "SPI",
      "USB"};
  for (std::uint32_t type = 0; type < expected.size(); ++type)
    gw::test::require(
        glasswyrm::drm::connector_type_name(type) == expected[type],
        "connector type mapping");
  gw::test::require(glasswyrm::drm::connector_name(11, 2) == "HDMI-A-2",
                    "common connector name");
  gw::test::require(glasswyrm::drm::connector_name(0, 1) == "Unknown-0-1",
                    "known unknown connector fallback");
  gw::test::require(glasswyrm::drm::connector_name(99, 7) == "Unknown-99-7",
                    "future connector fallback");
  gw::test::require(glasswyrm::drm::connection_status_name(
                        glasswyrm::drm::ConnectionStatus::Connected) ==
                        "connected" &&
                        glasswyrm::drm::connection_status_name(
                            glasswyrm::drm::ConnectionStatus::Disconnected) ==
                            "disconnected" &&
                        glasswyrm::drm::connection_status_name(
                            glasswyrm::drm::ConnectionStatus::Unknown) ==
                            "unknown",
                    "connector status names are stable");
  return 0;
}
