#include "compositor/scene_vrr_validation.hpp"

#include <map>

namespace gw::compositor {
namespace {

bool cursor(const gwipc_surface_upsert& surface) noexcept {
  return surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_CURSOR;
}

bool metadata(const gwipc_surface_upsert& surface) noexcept {
  return surface.presentation_flags == GWIPC_SURFACE_PRESENTATION_METADATA_ONLY;
}

SceneVrrValidationResult reject(const gwipc_frame_result result,
                                std::string error) {
  return {result, 0, std::move(error)};
}

} // namespace

SceneVrrValidationResult validate_scene_vrr(const Scene& scene,
                                             const bool required) noexcept {
  if (!required) {
    if (!scene.vrr.output_policies.empty() || !scene.vrr.surfaces.empty())
      return reject(GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA,
                    "VRR records were supplied without negotiation");
    return {};
  }
  if (scene.vrr.output_policies.size() != scene.outputs.size())
    return reject(GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA,
                  "M14 snapshot requires one VRR policy per output");
  for (const auto& [output_id, output] : scene.outputs) {
    static_cast<void>(output);
    const auto policy = scene.vrr.output_policies.find(output_id);
    if (policy == scene.vrr.output_policies.end() ||
        policy->second.output_id != output_id ||
        !valid_output_vrr_policy(policy->second))
      return reject(GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA,
                    "VRR policy references an unknown or invalid output");
  }

  std::size_t expected_surfaces = 0;
  std::map<std::uint64_t, std::uint64_t> selected_by_output;
  std::uint64_t generation = 0;
  for (const auto& [surface_id, surface] : scene.surfaces) {
    const auto record = scene.vrr.surfaces.find(surface_id);
    if (cursor(surface) || metadata(surface)) {
      if (record != scene.vrr.surfaces.end())
        return reject(GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA,
                      cursor(surface)
                          ? "cursor cannot carry a window VRR record"
                          : "metadata surface cannot carry a window VRR record");
      continue;
    }
    ++expected_surfaces;
    if (record == scene.vrr.surfaces.end() ||
        !valid_surface_vrr_state(record->second))
      return reject(GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA,
                    "window surface is missing valid VRR state");
    const auto& state = record->second;
    const auto membership = scene.surface_outputs.find(surface_id);
    if (state.surface_id != surface_id ||
        state.window_id != surface.x11_window_id ||
        !scene.outputs.contains(state.output_id) ||
        (membership != scene.surface_outputs.end() &&
         membership->second.primary_output_id != state.output_id))
      return reject(GWIPC_FRAME_REJECTED_UNKNOWN_SURFACE,
                    "surface VRR identity does not match scene membership");
    if (generation == 0)
      generation = state.policy_generation;
    else if (state.policy_generation != generation)
      return reject(GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA,
                    "surface VRR policy generations are inconsistent");
    if (state.policy_selected) {
      if (!state.policy_eligible)
        return reject(GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA,
                      "selected VRR candidate is not eligible");
      if (!selected_by_output.emplace(state.output_id, surface_id).second)
        return reject(GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA,
                      "output has more than one selected VRR candidate");
    }
  }
  if (scene.vrr.surfaces.size() != expected_surfaces)
    return reject(GWIPC_FRAME_REJECTED_UNKNOWN_SURFACE,
                  "VRR state references an unknown surface");
  if (!scene.vrr.surfaces.empty() &&
      scene.vrr.policy_generation != generation)
    return reject(GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA,
                  "VRR snapshot policy generation is inconsistent");
  return {GWIPC_FRAME_ACCEPTED,
          generation != 0 ? generation : scene.configuration_generation, {}};
}

} // namespace gw::compositor
