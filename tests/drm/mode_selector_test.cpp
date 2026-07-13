#include "backends/drm/mode_selector.hpp"

#include "tests/helpers/test_support.hpp"

#include <array>

int main() {
  using namespace glasswyrm::drm;
  const ModeRequest request{1024, 768, 60'000, std::nullopt, 1000};

  const std::array wrong_size{Mode{"800x600", 800, 600, 60'000, 40'000}};
  gw::test::require(select_mode(wrong_size, request).status ==
                        ModeSelectionStatus::NoMatchingDimensions,
                    "exact dimensions required");

  const std::array preferred{
      Mode{"1024x768", 1024, 768, 60'000, 65'000, false},
      Mode{"1024x768", 1024, 768, 59'940, 64'000, true}};
  gw::test::require(select_mode(preferred, request).mode_index == 1,
                    "preferred mode wins before nearest refresh");

  const std::array nearest{
      Mode{"near-low", 1024, 768, 59'800, 64'000},
      Mode{"far-high", 1024, 768, 60'500, 65'000}};
  gw::test::require(select_mode(nearest, request).mode_index == 0,
                    "nearest requested refresh");

  auto explicit_request = request;
  explicit_request.explicit_refresh_millihz = 60'000;
  const std::array explicit_modes{
      Mode{"preferred", 1024, 768, 59'000, 60'000, true},
      Mode{"explicit", 1024, 768, 60'000, 65'000, false}};
  gw::test::require(select_mode(explicit_modes, explicit_request).mode_index == 1,
                    "explicit refresh overrides preferred flag");

  const std::array too_far{Mode{"slow", 1024, 768, 58'999, 60'000, true}};
  gw::test::require(select_mode(too_far, request).status ==
                        ModeSelectionStatus::RefreshOutsideTolerance,
                    "refresh tolerance enforced");

  const std::array high_tie{
      Mode{"low", 1024, 768, 59'900, 60'000},
      Mode{"high", 1024, 768, 60'100, 60'000}};
  gw::test::require(select_mode(high_tie, request).mode_index == 1,
                    "higher refresh breaks equal distance tie");

  const std::array name_tie{
      Mode{"z-mode", 1024, 768, 60'000, 64'000},
      Mode{"a-mode", 1024, 768, 60'000, 65'000},
      Mode{"a-mode", 1024, 768, 60'000, 63'000}};
  gw::test::require(select_mode(name_tie, request).mode_index == 2,
                    "name then lower clock deterministically break ties");

  auto no_refresh = request;
  no_refresh.requested_refresh_millihz = 0;
  const std::array highest{
      Mode{"low", 1024, 768, 50'000, 50'000},
      Mode{"high", 1024, 768, 75'000, 75'000}};
  gw::test::require(select_mode(highest, no_refresh).mode_index == 1,
                    "highest refresh used without a requested refresh");
  return 0;
}
