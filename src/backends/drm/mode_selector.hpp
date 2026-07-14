#pragma once

#include "backends/drm/resources.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace glasswyrm::drm {

inline constexpr std::uint32_t kDefaultRefreshToleranceMillihz = 1000;

struct ModeRequest {
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t requested_refresh_millihz{};
  std::optional<std::uint32_t> explicit_refresh_millihz;
  std::uint32_t tolerance_millihz{kDefaultRefreshToleranceMillihz};
};

enum class ModeSelectionStatus {
  Success,
  NoMatchingDimensions,
  RefreshOutsideTolerance,
};

struct ModeSelection {
  ModeSelectionStatus status{ModeSelectionStatus::NoMatchingDimensions};
  std::size_t mode_index{};
};

[[nodiscard]] ModeSelection select_mode(std::span<const Mode> modes,
                                        const ModeRequest& request) noexcept;

}  // namespace glasswyrm::drm
