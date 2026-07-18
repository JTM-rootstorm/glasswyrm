#pragma once

#include "render/scene_renderer.hpp"

namespace gw::render::software {

class SoftwareSceneRenderer final : public SceneRenderer {
public:
  [[nodiscard]] RenderFrameResult
  render(const RenderFrameRequest& request) override;
  void disconnect() noexcept override {}
};

} // namespace gw::render::software
