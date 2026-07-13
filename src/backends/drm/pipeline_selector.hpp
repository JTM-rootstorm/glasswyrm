#pragma once

#include "backends/drm/resources.hpp"

#include <cstddef>
#include <span>

namespace glasswyrm::drm {

enum class CrtcSelectionStatus { Success, NoCompatibleCrtc, NoAvailableCrtc };

struct CrtcSelection {
  CrtcSelectionStatus status{CrtcSelectionStatus::NoCompatibleCrtc};
  std::size_t crtc_index{};
};

[[nodiscard]] CrtcSelection select_crtc(
    const Connector& connector, std::span<const Crtc> crtcs) noexcept;

enum class PlaneSelectionStatus {
  Success,
  ActiveNonPrimaryPlane,
  NoPrimaryPlane,
  NoCompatiblePrimaryPlane,
  Xrgb8888Unsupported,
};

struct PlaneSelection {
  PlaneSelectionStatus status{PlaneSelectionStatus::NoPrimaryPlane};
  std::size_t plane_index{};
};

[[nodiscard]] PlaneSelection select_primary_plane(
    const Crtc& crtc, std::span<const Plane> planes) noexcept;

}  // namespace glasswyrm::drm
