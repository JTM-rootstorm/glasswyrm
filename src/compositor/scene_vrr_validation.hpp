#pragma once

#include "compositor/scene.hpp"

#include <cstdint>
#include <string>

namespace gw::compositor {

struct SceneVrrValidationResult {
  gwipc_frame_result result{GWIPC_FRAME_ACCEPTED};
  std::uint64_t policy_generation{};
  std::string error;

  [[nodiscard]] bool accepted() const noexcept {
    return result == GWIPC_FRAME_ACCEPTED;
  }
};

[[nodiscard]] SceneVrrValidationResult
validate_scene_vrr(const Scene& scene, bool required) noexcept;

} // namespace gw::compositor
