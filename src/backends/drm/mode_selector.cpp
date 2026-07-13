#include "backends/drm/mode_selector.hpp"

#include <algorithm>
#include <limits>

namespace glasswyrm::drm {
namespace {

std::uint64_t refresh_distance(const std::uint32_t refresh,
                               const std::uint32_t target) noexcept {
  return refresh > target ? static_cast<std::uint64_t>(refresh - target)
                          : static_cast<std::uint64_t>(target - refresh);
}

bool mode_precedes(const Mode& candidate, const Mode& selected,
                   const ModeRequest& request) noexcept {
  const bool explicit_refresh = request.explicit_refresh_millihz.has_value();
  if (!explicit_refresh && candidate.preferred != selected.preferred)
    return candidate.preferred;
  const auto target = request.explicit_refresh_millihz.value_or(
      request.requested_refresh_millihz);
  if (target != 0) {
    const auto candidate_distance =
        refresh_distance(candidate.refresh_millihz, target);
    const auto selected_distance = refresh_distance(selected.refresh_millihz,
                                                    target);
    if (candidate_distance != selected_distance)
      return candidate_distance < selected_distance;
  }
  if (candidate.refresh_millihz != selected.refresh_millihz)
    return candidate.refresh_millihz > selected.refresh_millihz;
  if (candidate.name != selected.name) return candidate.name < selected.name;
  return candidate.clock_khz < selected.clock_khz;
}

}  // namespace

ModeSelection select_mode(const std::span<const Mode> modes,
                          const ModeRequest& request) noexcept {
  std::size_t selected = std::numeric_limits<std::size_t>::max();
  for (std::size_t index = 0; index < modes.size(); ++index) {
    const auto& mode = modes[index];
    if (mode.width != request.width || mode.height != request.height) continue;
    if (selected == std::numeric_limits<std::size_t>::max() ||
        mode_precedes(mode, modes[selected], request))
      selected = index;
  }
  if (selected == std::numeric_limits<std::size_t>::max())
    return {ModeSelectionStatus::NoMatchingDimensions, 0};

  const auto target = request.explicit_refresh_millihz.value_or(
      request.requested_refresh_millihz);
  if (target != 0 &&
      refresh_distance(modes[selected].refresh_millihz, target) >
          request.tolerance_millihz)
    return {ModeSelectionStatus::RefreshOutsideTolerance, selected};
  return {ModeSelectionStatus::Success, selected};
}

}  // namespace glasswyrm::drm
