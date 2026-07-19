#include "glasswyrmd/output_control_windows.hpp"

#include <algorithm>
#include <new>

namespace glasswyrm::server {
namespace {

std::uint32_t client_scale(const WindowScaleState &scale) noexcept {
  return scale.presentation == WindowScalePresentationState::ScaleAwareActive
             ? scale.accepted_buffer_scale
             : 1U;
}

gwipc_surface_scale_mode scale_mode(const WindowScaleState &scale) noexcept {
  return scale.presentation == WindowScalePresentationState::ScaleAwareActive
             ? GWIPC_SURFACE_SCALE_SCALED_PIXMAP
             : GWIPC_SURFACE_SCALE_LEGACY;
}

gwipc_sdr_color_metadata color() noexcept {
  return {GWIPC_SDR_COLOR_SPACE_SRGB, GWIPC_TRANSFER_FUNCTION_SRGB,
          GWIPC_COLOR_PRIMARIES_SRGB, 0, 0, 0, 0};
}

} // namespace

std::optional<std::vector<compositor::OutputInventoryWindow>>
build_output_control_windows(const LifecycleSnapshot &snapshot,
                             const output::OutputLayout &layout) {
  try {
    std::vector<compositor::OutputInventoryWindow> result;
    result.reserve(snapshot.windows.size());
    for (const auto &[xid, window] : snapshot.windows) {
      const auto primary =
          layout.states.find(output::OutputId{window.assigned_output_id});
      if (xid == 0 || window.applied_width == 0 ||
          window.applied_height == 0 || primary == layout.states.end() ||
          !primary->second.enabled)
        return std::nullopt;
      compositor::OutputInventoryWindow item;
      item.surface.struct_size = sizeof(item.surface);
      item.surface.surface_id = (UINT64_C(1) << 32U) | xid;
      item.surface.x11_window_id = xid;
      item.surface.output_id = window.assigned_output_id;
      item.surface.logical_x = window.applied_x;
      item.surface.logical_y = window.applied_y;
      item.surface.logical_width = window.applied_width;
      item.surface.logical_height = window.applied_height;
      item.surface.stacking = window.stacking;
      item.surface.visible = window.policy_visible;
      item.surface.transform = GWIPC_TRANSFORM_NORMAL;
      item.surface.opacity = GWIPC_OPACITY_ONE;
      item.surface.scale_numerator = client_scale(window.scale);
      item.surface.scale_denominator = 1;
      item.surface.color = color();
      item.surface.presentation_flags =
          GWIPC_SURFACE_PRESENTATION_METADATA_ONLY;

      item.policy.struct_size = sizeof(item.policy);
      item.policy.surface_id = item.surface.surface_id;
      item.policy.x11_window_id = xid;
      item.policy.workspace_id = snapshot.workspace_id;
      item.policy.window_type =
          static_cast<gwipc_policy_window_type>(window.window_type);
      item.policy.applied_state =
          window.applied_state >= GWIPC_POLICY_APPLIED_NORMAL &&
                  window.applied_state <= GWIPC_POLICY_APPLIED_MINIMIZED
              ? static_cast<gwipc_policy_applied_state>(window.applied_state)
              : GWIPC_POLICY_APPLIED_NORMAL;
      item.policy.focused = window.focused;
      item.policy.managed = window.managed;
      item.policy.decoration_eligible = window.decoration_eligible;
      item.policy.override_redirect = window.override_redirect;
      item.policy.attention_requested = window.attention_requested;
      item.policy.fullscreen_eligible =
          static_cast<gwipc_tri_state>(window.fullscreen_eligible);
      item.policy.direct_scanout_eligible =
          static_cast<gwipc_tri_state>(window.direct_scanout_eligible);

      item.output_ids = window.output_memberships;
      item.membership.struct_size = sizeof(item.membership);
      item.membership.surface_id = item.surface.surface_id;
      item.membership.primary_output_id = window.assigned_output_id;
      item.membership.output_count = item.output_ids.size();
      item.membership.preferred_scale_numerator =
          window.scale.has_output_state
              ? window.scale.preferred_scale_numerator
              : primary->second.scale.numerator;
      item.membership.preferred_scale_denominator =
          window.scale.has_output_state
              ? window.scale.preferred_scale_denominator
              : primary->second.scale.denominator;
      item.membership.client_buffer_scale = client_scale(window.scale);
      item.membership.scale_mode = scale_mode(window.scale);
      item.membership.layout_generation = layout.generation;
      result.push_back(std::move(item));
    }
    return result;
  } catch (const std::bad_alloc &) {
    return std::nullopt;
  }
}

} // namespace glasswyrm::server
