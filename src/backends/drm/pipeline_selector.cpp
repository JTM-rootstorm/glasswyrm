#include "backends/drm/pipeline_selector.hpp"

#include <algorithm>
#include <limits>

namespace glasswyrm::drm {
namespace {

bool mask_contains(const std::uint32_t mask,
                   const std::uint32_t index) noexcept {
  return index < 32 && (mask & (1U << index)) != 0;
}

bool supports_xrgb8888(const Plane& plane) noexcept {
  return std::find(plane.formats.begin(), plane.formats.end(),
                   kFormatXrgb8888) != plane.formats.end();
}

}  // namespace

CrtcSelection select_crtc(const Connector& connector,
                          const std::span<const Crtc> crtcs) noexcept {
  bool compatible_exists = false;
  if (connector.current_crtc_id != 0) {
    for (std::size_t index = 0; index < crtcs.size(); ++index) {
      const auto& crtc = crtcs[index];
      if (crtc.id == connector.current_crtc_id &&
          mask_contains(connector.possible_crtc_mask, crtc.index))
        return {CrtcSelectionStatus::Success, index};
    }
  }

  std::size_t selected = std::numeric_limits<std::size_t>::max();
  for (std::size_t index = 0; index < crtcs.size(); ++index) {
    const auto& crtc = crtcs[index];
    if (!mask_contains(connector.possible_crtc_mask, crtc.index)) continue;
    compatible_exists = true;
    if (!crtc.connector_ids.empty()) continue;
    if (selected == std::numeric_limits<std::size_t>::max() ||
        crtc.id < crtcs[selected].id)
      selected = index;
  }
  if (selected != std::numeric_limits<std::size_t>::max())
    return {CrtcSelectionStatus::Success, selected};
  return {compatible_exists ? CrtcSelectionStatus::NoAvailableCrtc
                            : CrtcSelectionStatus::NoCompatibleCrtc,
          0};
}

PlaneSelection select_primary_plane(
    const Crtc& crtc, const std::span<const Plane> planes) noexcept {
  if (std::any_of(planes.begin(), planes.end(), [&crtc](const Plane& plane) {
        return plane.current_crtc_id == crtc.id &&
               plane.type != PlaneType::Primary;
      }))
    return {PlaneSelectionStatus::ActiveNonPrimaryPlane, 0};

  bool primary_exists = false;
  bool compatible_exists = false;
  std::size_t selected = std::numeric_limits<std::size_t>::max();
  for (std::size_t index = 0; index < planes.size(); ++index) {
    const auto& plane = planes[index];
    if (plane.type != PlaneType::Primary) continue;
    primary_exists = true;
    if (!mask_contains(plane.possible_crtc_mask, crtc.index)) continue;
    compatible_exists = true;
    if (!supports_xrgb8888(plane)) continue;
    if (selected == std::numeric_limits<std::size_t>::max() ||
        (plane.current_crtc_id == crtc.id &&
         planes[selected].current_crtc_id != crtc.id) ||
        (plane.current_crtc_id == planes[selected].current_crtc_id &&
         plane.id < planes[selected].id))
      selected = index;
  }
  if (selected != std::numeric_limits<std::size_t>::max())
    return {PlaneSelectionStatus::Success, selected};
  if (!primary_exists) return {PlaneSelectionStatus::NoPrimaryPlane, 0};
  if (!compatible_exists)
    return {PlaneSelectionStatus::NoCompatiblePrimaryPlane, 0};
  return {PlaneSelectionStatus::Xrgb8888Unsupported, 0};
}

}  // namespace glasswyrm::drm
