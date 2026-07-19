#include "gwcomp/drm_runtime.hpp"

#include "backends/drm/resources.hpp"
#include "tests/helpers/test_support.hpp"

#include <cstdint>
#include <string>

namespace {

using namespace glasswyrm;

drm::Connector connector(const std::uint32_t type_id) {
  drm::Connector value;
  value.id = 70 + type_id;
  value.type = static_cast<std::uint32_t>(drm::ConnectorType::DisplayPort);
  value.type_id = type_id;
  value.status = drm::ConnectionStatus::Connected;
  value.possible_crtc_mask = 1;
  return value;
}

drm::DeviceSnapshot snapshot() {
  drm::DeviceSnapshot value;
  value.crtcs.push_back({40, 0, {}});
  auto output = connector(1);
  output.modes = {
      drm::Mode{"1920x1080", 1920, 1080, 60'000, 148'500, false},
      drm::Mode{"z-mode", 1024, 768, 60'000, 65'000, true},
      drm::Mode{"a-mode", 1024, 768, 60'000, 65'000, true},
      drm::Mode{"a-mode", 1024, 768, 60'000, 63'000, true},
  };
  value.connectors.push_back(std::move(output));
  return value;
}

} // namespace

int main() {
  using namespace glasswyrm;
  using gw::test::require;

  auto discovered = snapshot();
  compositor::Options options;
  auto selection =
      compositor::resolve_drm_output_selection(discovered, options);
  require(selection && selection->connector_index == 0 &&
              selection->mode_index == 3,
          "default selection retains exact preferred mode tie-break index");

  options.mode = compositor::RequestedMode{1920, 1080, std::nullopt};
  selection = compositor::resolve_drm_output_selection(discovered, options);
  require(selection && selection->connector_index == 0 &&
              selection->mode_index == 0,
          "explicit dimensions retain the mode selector result index");

  options.mode = compositor::RequestedMode{1024, 768, 60'000};
  selection = compositor::resolve_drm_output_selection(discovered, options);
  require(selection && selection->mode_index == 3,
          "explicit refresh preserves deterministic name and clock tie-breaks");

  auto outside_tolerance = snapshot();
  outside_tolerance.connectors[0].modes = {
      drm::Mode{"1024x768", 1024, 768, 61'001, 65'000, true}};
  require(!compositor::resolve_drm_output_selection(outside_tolerance, options),
          "explicit refresh outside the historical tolerance is rejected");

  auto ambiguous = snapshot();
  auto second = connector(2);
  second.modes.push_back(drm::Mode{"800x600", 800, 600, 60'000, 40'000, true});
  ambiguous.connectors.push_back(std::move(second));
  options.mode.reset();
  require(!compositor::resolve_drm_output_selection(ambiguous, options),
          "unconstrained selection preserves multi-connector ambiguity");

  options.connector = "DP-2";
  selection = compositor::resolve_drm_output_selection(ambiguous, options);
  require(selection && selection->connector_index == 1 &&
              selection->mode_index == 0,
          "explicit connector resolves unconstrained ambiguity exactly");

  options.connector = "DP-9";
  require(!compositor::resolve_drm_output_selection(ambiguous, options),
          "unknown explicit connector remains unsupported");

  options.connector = "DP-1";
  ambiguous.connectors[0].non_desktop = true;
  require(!compositor::resolve_drm_output_selection(ambiguous, options),
          "non-desktop connector remains ineligible without a mode request");
  return 0;
}
