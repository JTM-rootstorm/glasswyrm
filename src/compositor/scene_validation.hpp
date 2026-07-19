#pragma once

#include "compositor/scene.hpp"

namespace gw::compositor {

[[nodiscard]] bool valid_scene_output(const gwipc_output_upsert &output,
                                      SceneProfile profile) noexcept;
[[nodiscard]] bool valid_scene_surface(const gwipc_surface_upsert &surface,
                                       SceneProfile profile) noexcept;
[[nodiscard]] bool
valid_surface_output_state(const gwipc_surface_output_state &state) noexcept;

void infer_historical_output_state(Scene &scene);

[[nodiscard]] gwipc_frame_result
validate_output_model_scene(const Scene &scene) noexcept;

} // namespace gw::compositor
