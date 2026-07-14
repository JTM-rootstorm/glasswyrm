#include "backends/drm/connector_selector.hpp"

#include "tests/helpers/test_support.hpp"

#include <array>
#include <string_view>

namespace {

glasswyrm::drm::Connector connector(const std::uint32_t id,
                                    const std::uint32_t type_id) {
  using namespace glasswyrm::drm;
  Connector value;
  value.id = id;
  value.type = static_cast<std::uint32_t>(ConnectorType::Virtual);
  value.type_id = type_id;
  value.status = ConnectionStatus::Connected;
  value.modes.push_back({"1024x768", 1024, 768, 60'000, 65'000, true});
  value.possible_crtc_mask = 1;
  return value;
}

}  // namespace

int main() {
  using namespace glasswyrm::drm;
  const std::array crtcs{Crtc{40, 0, {}}};
  auto value = connector(10, 1);
  std::array connectors{value};
  gw::test::require(select_connector(connectors, crtcs, 1024, 768).status ==
                        ConnectorSelectionStatus::Success,
                    "one exact connector selected");

  connectors[0].status = ConnectionStatus::Disconnected;
  gw::test::require(select_connector(connectors, crtcs, 1024, 768,
                                     std::string_view{"Virtual-1"}).status ==
                        ConnectorSelectionStatus::Disconnected,
                    "disconnected explicit connector rejected");
  connectors[0] = value;
  connectors[0].modes.clear();
  gw::test::require(select_connector(connectors, crtcs, 1024, 768,
                                     std::string_view{"Virtual-1"}).status ==
                        ConnectorSelectionStatus::NoModes,
                    "modeless connector rejected");
  connectors[0] = value;
  connectors[0].non_desktop = true;
  gw::test::require(select_connector(connectors, crtcs, 1024, 768,
                                     std::string_view{"Virtual-1"}).status ==
                        ConnectorSelectionStatus::NonDesktop,
                    "non-desktop connector rejected");
  connectors[0] = value;
  connectors[0].type = static_cast<std::uint32_t>(ConnectorType::Writeback);
  gw::test::require(select_connector(connectors, crtcs, 1024, 768,
                                     std::string_view{"Writeback-1"}).status ==
                        ConnectorSelectionStatus::Writeback,
                    "writeback connector rejected");
  connectors[0] = value;
  connectors[0].possible_crtc_mask = 0;
  gw::test::require(select_connector(connectors, crtcs, 1024, 768,
                                     std::string_view{"Virtual-1"}).status ==
                        ConnectorSelectionStatus::NoCompatibleCrtc,
                    "connector route required");
  connectors[0].possible_crtc_mask = 0b10;
  gw::test::require(select_connector(connectors, crtcs, 1024, 768,
                                     std::string_view{"Virtual-1"}).status ==
                        ConnectorSelectionStatus::NoCompatibleCrtc,
                    "route must address an enumerated CRTC");
  connectors[0] = value;
  gw::test::require(select_connector(connectors, crtcs, 800, 600,
                                     std::string_view{"Virtual-1"}).status ==
                        ConnectorSelectionStatus::NoMatchingMode,
                    "exact output mode required");

  const std::array ambiguous{value, connector(11, 2)};
  gw::test::require(select_connector(ambiguous, crtcs, 1024, 768).status ==
                        ConnectorSelectionStatus::Ambiguous,
                    "auto selection rejects ambiguity");
  gw::test::require(select_connector(ambiguous, crtcs, 1024, 768,
                                     std::string_view{"Virtual-2"})
                            .connector_index == 1,
                    "explicit connector name resolves ambiguity");
  gw::test::require(select_connector(ambiguous, crtcs, 1024, 768,
                                     std::string_view{"DP-9"}).status ==
                        ConnectorSelectionStatus::NotFound,
                    "unknown explicit name rejected");

  auto cloned = value;
  cloned.current_crtc_id = 40;
  const std::array cloned_connectors{cloned};
  const std::array cloned_crtcs{Crtc{40, 0, {10, 11}}};
  gw::test::require(select_connector(cloned_connectors, cloned_crtcs, 1024,
                                     768, std::string_view{"Virtual-1"})
                            .status == ConnectorSelectionStatus::ClonedCrtc,
                    "cloned current CRTC rejected");
  return 0;
}
