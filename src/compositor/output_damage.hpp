#pragma once

#include "compositor/scene.hpp"

#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace gw::compositor {

using PhysicalOutputDamage =
    std::map<std::uint64_t, std::vector<Rectangle>>;

// Computes conservative native-output damage for one atomic output-model
// scene transition. Surface IDs in content_changed are treated as having new
// pixels even when their scene metadata did not change.
[[nodiscard]] PhysicalOutputDamage calculate_output_damage(
    const Scene& before, const Scene& after,
    std::span<const std::uint64_t> content_changed);

} // namespace gw::compositor
