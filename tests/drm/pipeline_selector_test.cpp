#include "backends/drm/pipeline_selector.hpp"

#include "tests/helpers/test_support.hpp"

#include <array>

int main() {
  using namespace glasswyrm::drm;
  Connector connector;
  connector.id = 10;
  connector.possible_crtc_mask = 0b111;
  connector.current_crtc_id = 30;
  const std::array crtcs{
      Crtc{20, 0, {}}, Crtc{30, 1, {10}}, Crtc{10, 2, {}}};
  gw::test::require(select_crtc(connector, crtcs).crtc_index == 1,
                    "compatible current CRTC preferred");

  connector.current_crtc_id = 0;
  gw::test::require(select_crtc(connector, crtcs).crtc_index == 2,
                    "lowest ID unused compatible CRTC selected");

  connector.possible_crtc_mask = 0b010;
  gw::test::require(select_crtc(connector, crtcs).status ==
                        CrtcSelectionStatus::NoAvailableCrtc,
                    "CRTC serving unrelated connector is not stolen");
  connector.possible_crtc_mask = 0b1000;
  gw::test::require(select_crtc(connector, crtcs).status ==
                        CrtcSelectionStatus::NoCompatibleCrtc,
                    "no compatible CRTC reported");

  const Crtc selected_crtc{30, 1, {10}};
  const Plane eligible{50, PlaneType::Primary, 0b010,
                       {kFormatXrgb8888}, 0};
  const std::array one_plane{eligible};
  gw::test::require(select_primary_plane(selected_crtc, one_plane).status ==
                        PlaneSelectionStatus::Success,
                    "compatible XRGB primary plane selected");

  const std::array no_primary{
      Plane{51, PlaneType::Cursor, 0b010, {kFormatXrgb8888}, 0}};
  gw::test::require(select_primary_plane(selected_crtc, no_primary).status ==
                        PlaneSelectionStatus::NoPrimaryPlane,
                    "primary plane required");
  const std::array incompatible{
      Plane{51, PlaneType::Primary, 0b001, {kFormatXrgb8888}, 0}};
  gw::test::require(
      select_primary_plane(selected_crtc, incompatible).status ==
          PlaneSelectionStatus::NoCompatiblePrimaryPlane,
      "plane CRTC mask enforced");
  const std::array wrong_format{
      Plane{51, PlaneType::Primary, 0b010, {fourcc('A', 'R', '2', '4')}, 0}};
  gw::test::require(select_primary_plane(selected_crtc, wrong_format).status ==
                        PlaneSelectionStatus::Xrgb8888Unsupported,
                    "XRGB8888 required");

  const std::array active_overlay{
      eligible,
      Plane{60, PlaneType::Overlay, 0b010, {kFormatXrgb8888}, 30}};
  gw::test::require(
      select_primary_plane(selected_crtc, active_overlay).status ==
          PlaneSelectionStatus::ActiveNonPrimaryPlane,
      "active overlay on selected CRTC rejected");

  const std::array preferred_attached{
      Plane{40, PlaneType::Primary, 0b010, {kFormatXrgb8888}, 0},
      Plane{70, PlaneType::Primary, 0b010, {kFormatXrgb8888}, 30},
      Plane{20, PlaneType::Primary, 0b010, {kFormatXrgb8888}, 0}};
  gw::test::require(select_primary_plane(selected_crtc, preferred_attached)
                            .plane_index == 1,
                    "currently attached primary plane preferred");
  const std::array lowest_id{
      Plane{40, PlaneType::Primary, 0b010, {kFormatXrgb8888}, 0},
      Plane{20, PlaneType::Primary, 0b010, {kFormatXrgb8888}, 0}};
  gw::test::require(select_primary_plane(selected_crtc, lowest_id).plane_index ==
                        1,
                    "lowest plane ID deterministically breaks tie");
  return 0;
}
