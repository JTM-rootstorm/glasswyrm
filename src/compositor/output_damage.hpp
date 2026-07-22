#pragma once

#include "compositor/scene.hpp"

#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace gw::compositor {

using PhysicalOutputDamage =
    std::map<std::uint64_t, std::vector<Rectangle>>;

enum class OutputDamageFallbackReason : std::uint8_t {
  OutputConfiguration = 0,
  SurfaceStructural = 1,
  RectangleLimit = 2,
  NewBuffer = 3,
  ReplacementBuffer = 4,
  Untrusted = 5,
};

struct OutputDamageResult {
  PhysicalOutputDamage regions;
  std::map<std::uint64_t, std::vector<OutputDamageFallbackReason>>
      fallback_reasons;
};

// Computes conservative native-output damage for one atomic output-model
// scene transition. Trusted complete rectangles remain exact; structural or
// untrusted surface changes use old/new full-surface bounds.
[[nodiscard]] OutputDamageResult
calculate_output_damage(const Scene &before, const Scene &after,
                        const SceneDamageResult &content_damage);

} // namespace gw::compositor
