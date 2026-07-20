#include "glasswyrmd/lifecycle_projection.hpp"

#include "glasswyrmd/output_scene_projection.hpp"
#include "glasswyrmd/gw_scale_state.hpp"
#include "glasswyrmd/vrr_policy_projection.hpp"
#include "ipc/vrr_membership_hint.hpp"

#include <algorithm>
#include <vector>

namespace glasswyrm::server {
namespace {

std::uint32_t bounded_extent(const std::uint32_t requested,
                             const std::uint32_t minimum,
                             const std::uint32_t maximum) {
  auto result = requested;
  if (minimum != 0) result = std::max(result, minimum);
  if (maximum != 0) result = std::min(result, maximum);
  return result;
}

void append_policy_outputs(PolicySnapshotSubmission& submission,
                           const output::OutputLayout& layout) {
  submission.outputs.reserve(layout.output_order.size());
  for (const auto id : layout.output_order) {
    const auto& state = layout.states.at(id);
    gwipc_policy_output_upsert record{};
    record.struct_size = sizeof(record);
    record.output_id = id.value;
    record.logical_x = state.logical_x;
    record.logical_y = state.logical_y;
    record.logical_width = state.logical_width;
    record.logical_height = state.logical_height;
    record.work_x = state.logical_x;
    record.work_y = state.logical_y;
    record.work_width = state.logical_width;
    record.work_height = state.logical_height;
    record.scale_numerator = state.scale.numerator;
    record.scale_denominator = state.scale.denominator;
    record.transform = static_cast<gwipc_transform>(state.transform);
    record.enabled = state.enabled;
    record.primary = state.primary;
    submission.outputs.push_back(record);
  }
}

std::vector<std::uint64_t> canonical_output_ids(
    const output::OutputLayout& layout) {
  std::vector<std::uint64_t> result;
  result.reserve(layout.states.size());
  for (const auto& [id, unused] : layout.states) {
    static_cast<void>(unused);
    result.push_back(id.value);
  }
  return result;
}

gwipc_surface_scale_mode scale_mode(const WindowScaleState& scale) noexcept {
  return scale.presentation == WindowScalePresentationState::ScaleAwareActive
             ? GWIPC_SURFACE_SCALE_SCALED_PIXMAP
             : GWIPC_SURFACE_SCALE_LEGACY;
}

std::uint32_t client_scale(const WindowScaleState& scale) noexcept {
  return scale.presentation == WindowScalePresentationState::ScaleAwareActive
             ? scale.accepted_buffer_scale
             : 1U;
}

void apply_projected_scale(LifecycleWindow& window,
                           const SurfaceOutputProjection& projected) {
  window.assigned_output_id = projected.primary_output_id;
  window.output_memberships = projected.output_ids;
  window.scale.has_output_state = true;
  window.scale.preferred_scale_numerator =
      projected.preferred_scale_numerator;
  window.scale.preferred_scale_denominator =
      projected.preferred_scale_denominator;
  window.scale.layout_generation = projected.layout_generation;
}

}  // namespace

PolicySnapshotSubmission project_policy(const LifecycleSnapshot& snapshot,
                                        std::uint64_t commit,
                                        std::uint64_t generation,
                                        const output::OutputLayout* layout,
                                        const VrrStateCache* vrr) {
  PolicySnapshotSubmission output{commit, generation, {}, {}, {}};
  const auto output_ids = layout ? canonical_output_ids(*layout)
                                 : std::vector<std::uint64_t>{};
  for (const auto& [id, value] : snapshot.windows) {
    (void)id;
    gwipc_policy_lifecycle_window_upsert item{}; item.struct_size=sizeof(item);
    item.window.struct_size=sizeof(item.window); item.window.window_id=value.xid;
    item.window.parent_window_id=value.parent; item.window.transient_for=value.transient_for;
    item.window.workspace_id=snapshot.workspace_id;
    item.window.requested_x=value.requested_x; item.window.requested_y=value.requested_y;
    item.window.requested_width=bounded_extent(value.requested_width,value.minimum_width,value.maximum_width);
    item.window.requested_height=bounded_extent(value.requested_height,value.minimum_height,value.maximum_height);
    item.window.border_width=value.requested_border_width;
    item.window.window_type=static_cast<gwipc_policy_window_type>(value.policy_window_type);
    item.window.map_intent=value.map_requested?GWIPC_POLICY_WANTS_MAP:GWIPC_POLICY_UNMAPPED;
    item.window.override_redirect=value.override_redirect;
    item.window.decoration_preference=static_cast<gwipc_tri_state>(value.decoration_preference);
    item.window.fullscreen_requested=value.fullscreen_requested;
    item.window.maximized_requested=value.maximized_requested;
    item.window.attention_requested=value.attention_requested;
    item.window.creation_serial=value.creation_serial; item.window.map_serial=value.map_serial;
    item.window.focus_serial=value.focus_serial;
    item.window.flags=
        (value.above_requested
             ? static_cast<std::uint32_t>(GWIPC_POLICY_WINDOW_FLAG_ABOVE)
             : 0U)|
        (value.bypass_compositor
             ? static_cast<std::uint32_t>(
                   GWIPC_POLICY_WINDOW_FLAG_BYPASS_COMPOSITOR)
             : 0U)|
        (!value.input_requested
             ? static_cast<std::uint32_t>(
                   GWIPC_POLICY_WINDOW_FLAG_INPUT_DISABLED)
             : 0U);
    item.geometry_serial=value.geometry_serial;
    item.stack_serial=value.stack_serial; item.stack_sibling=value.stack_sibling;
    item.stack_mode=static_cast<gwipc_policy_stack_mode>(value.stack_mode);
    output.windows.push_back(item);
    if (layout && (value.assigned_output_id != 0 || vrr)) {
      gwipc_policy_window_output_hint hint{};
      hint.struct_size = sizeof(hint);
      hint.window_id = value.xid;
      hint.previous_output_id = value.assigned_output_id;
      if (vrr) {
        auto membership = value.output_memberships;
        std::ranges::sort(membership);
        membership.erase(std::unique(membership.begin(), membership.end()),
                         membership.end());
        const auto encoded =
            glasswyrm::ipc::internal::encode_vrr_membership_hint(
                output_ids, membership);
        if (!encoded) return {};
        hint.preferred_output_id = *encoded;
      }
      output.output_hints.push_back(hint);
    }
  }
  if (layout) append_policy_outputs(output, *layout);
  if (layout && vrr) output.vrr = project_vrr_policy(snapshot, *layout, *vrr);
  return output;
}

std::optional<LifecycleSnapshot> apply_policy_result(
    const LifecycleSnapshot& proposed, const PolicySnapshotResult& result,
    const output::OutputLayout* layout, VrrStateCache* vrr) {
  if (result.windows.size()!=proposed.windows.size()) return std::nullopt;
  auto evaluated=proposed; evaluated.focused_window=proposed.root_window;
  std::vector<std::pair<std::int32_t,std::uint32_t>> visible;
  for(const auto& state:result.windows){
    auto found=evaluated.windows.find(state.window_id); if(found==evaluated.windows.end())return std::nullopt;
    const bool valid_output = layout
        ? layout->states.contains(output::OutputId{state.output_id}) &&
              layout->states.at(output::OutputId{state.output_id}).enabled
        : state.output_id == proposed.output_id;
    if(state.workspace_id!=proposed.workspace_id||!valid_output||
       state.final_width==0||state.final_height==0||state.final_width>UINT16_MAX||state.final_height>UINT16_MAX||
       state.final_x<INT16_MIN||state.final_x>INT16_MAX||state.final_y<INT16_MIN||state.final_y>INT16_MAX||
       state.window_type>GWIPC_POLICY_WINDOW_UTILITY||state.applied_state<GWIPC_POLICY_APPLIED_NORMAL||state.applied_state>GWIPC_POLICY_APPLIED_MINIMIZED||
       state.visible>1||state.focused>1||state.managed>1||state.decoration_eligible>1||state.override_redirect>1||state.attention_requested>1||
       state.fullscreen_eligible>GWIPC_TRI_STATE_TRUE||state.direct_scanout_eligible>GWIPC_TRI_STATE_TRUE)return std::nullopt;
    auto& window=found->second;
    if (window.applied_width != 0 && window.applied_height != 0 &&
        (window.applied_width != state.final_width ||
         window.applied_height != state.final_height))
      (void)invalidate_scaled_pixmap(window.scale);
    window.applied_x=state.final_x;window.applied_y=state.final_y;
    window.applied_width=state.final_width;window.applied_height=state.final_height;
    window.stacking=state.stacking;window.policy_visible=state.visible;window.focused=state.focused;
    window.window_type=static_cast<std::uint8_t>(state.window_type);window.applied_state=static_cast<std::uint8_t>(state.applied_state);
    window.managed=state.managed;window.decoration_eligible=state.decoration_eligible;window.override_redirect=state.override_redirect;
    window.attention_requested=state.attention_requested;window.fullscreen_eligible=static_cast<std::uint8_t>(state.fullscreen_eligible);
    window.direct_scanout_eligible=static_cast<std::uint8_t>(state.direct_scanout_eligible);
    if (layout) {
      const auto projected = project_surface_outputs(
          *layout, state.output_id, state.final_x, state.final_y,
          state.final_width, state.final_height, state.visible != 0,
          client_scale(window.scale), scale_mode(window.scale));
      if (!projected) return std::nullopt;
      apply_projected_scale(window, *projected);
    }
    if(state.focused){if(evaluated.focused_window!=proposed.root_window||!state.visible||!state.managed)return std::nullopt;evaluated.focused_window=state.window_id;}
    if(state.visible)visible.emplace_back(state.stacking,state.window_id);else if(state.stacking!=-1)return std::nullopt;
  }
  std::sort(visible.begin(),visible.end());
  for(std::size_t i=0;i<visible.size();++i)if(visible[i].first!=static_cast<std::int32_t>(i))return std::nullopt;
  std::vector<std::uint32_t> order;
  for(const auto id:proposed.root_order)
    if(std::none_of(visible.begin(),visible.end(),[&](const auto& item){return item.second==id;}))order.push_back(id);
  for(auto [stack,id]:visible){(void)stack;order.push_back(id);}
  evaluated.root_order=std::move(order);
  if (vrr &&
      !vrr->stage_policy_result(result.generation, result.vrr_outputs,
                                result.vrr_windows))
    return std::nullopt;
  return evaluated;
}

bool policy_output_facts_match(const LifecycleSnapshot& policy_input,
                               const LifecycleSnapshot& evaluated) noexcept {
  if (policy_input.windows.size() != evaluated.windows.size()) return false;
  for (const auto& [window_id, before] : policy_input.windows) {
    const auto after = evaluated.windows.find(window_id);
    if (after == evaluated.windows.end() ||
        before.assigned_output_id != after->second.assigned_output_id ||
        before.output_memberships != after->second.output_memberships)
      return false;
  }
  return true;
}

CompositorSnapshotSubmission project_compositor(const LifecycleSnapshot& snapshot,
                                                std::uint64_t commit,
                                                std::uint64_t generation,
                                                const bool software_content,
                                                const output::OutputLayout* layout,
                                                VrrStateCache* vrr) {
  CompositorSnapshotSubmission output{commit,generation,{},{},{},{}};
  for(const auto& [id,value]:snapshot.windows){
    gwipc_surface_upsert surface{};surface.struct_size=sizeof(surface);surface.surface_id=(UINT64_C(1)<<32)|id;
    surface.x11_window_id=id;surface.output_id=layout?value.assigned_output_id:snapshot.output_id;surface.logical_x=value.applied_x;surface.logical_y=value.applied_y;
    surface.logical_width=value.applied_width;surface.logical_height=value.applied_height;surface.stacking=value.stacking;
    surface.visible=value.policy_visible;surface.transform=GWIPC_TRANSFORM_NORMAL;surface.opacity=GWIPC_OPACITY_ONE;
    surface.scale_numerator=layout?client_scale(value.scale):1;surface.scale_denominator=1;surface.color={GWIPC_SDR_COLOR_SPACE_SRGB,GWIPC_TRANSFER_FUNCTION_SRGB,GWIPC_COLOR_PRIMARIES_SRGB,0,0,0,0};
    surface.presentation_flags=software_content ? 0U : GWIPC_SURFACE_PRESENTATION_METADATA_ONLY;output.surfaces.push_back(surface);
    gwipc_surface_policy_upsert policy{};policy.struct_size=sizeof(policy);policy.surface_id=surface.surface_id;
    policy.x11_window_id=id;policy.workspace_id=snapshot.workspace_id;policy.window_type=static_cast<gwipc_policy_window_type>(value.window_type);
    policy.applied_state=static_cast<gwipc_policy_applied_state>(value.applied_state);policy.focused=value.focused;policy.managed=value.managed;
    policy.decoration_eligible=value.decoration_eligible;policy.override_redirect=value.override_redirect;policy.attention_requested=value.attention_requested;
    policy.fullscreen_eligible=static_cast<gwipc_tri_state>(value.fullscreen_eligible);policy.direct_scanout_eligible=static_cast<gwipc_tri_state>(value.direct_scanout_eligible);
    output.policies.push_back(policy);
    if (layout && software_content) {
      const auto projected = project_surface_outputs(
          *layout, surface.output_id, surface.logical_x, surface.logical_y,
          surface.logical_width, surface.logical_height, surface.visible != 0,
          surface.scale_numerator, scale_mode(value.scale));
      if (!projected) return {};
      CompositorSnapshotSubmission::SurfaceOutput membership;
      membership.output_ids = projected->output_ids;
      membership.state.struct_size = sizeof(membership.state);
      membership.state.surface_id = surface.surface_id;
      membership.state.primary_output_id = projected->primary_output_id;
      membership.state.output_count = membership.output_ids.size();
      membership.state.preferred_scale_numerator = projected->preferred_scale_numerator;
      membership.state.preferred_scale_denominator = projected->preferred_scale_denominator;
      membership.state.client_buffer_scale = projected->client_buffer_scale;
      membership.state.scale_mode = projected->scale_mode;
      membership.state.layout_generation = projected->layout_generation;
      output.surface_outputs.push_back(std::move(membership));
    }
  }
  if (layout && !populate_output_scene_records(output, *layout)) return {};
  if (layout && vrr) {
    for (const auto& [id, value] : vrr->outputs()) {
      static_cast<void>(id);
      output.output_vrr_policies.push_back(value.policy);
    }
    if (software_content) {
      output.surface_vrr_states =
          project_vrr_surfaces(snapshot, *vrr, vrr->generation());
      if (output.surface_vrr_states.size() != snapshot.windows.size() ||
          !vrr->stage_surface_states(output.surface_vrr_states))
        return {};
    }
  }
  return output;
}

std::vector<AppliedPolicyWindow> applied_policy(const LifecycleSnapshot& snapshot){
 std::vector<AppliedPolicyWindow> output;for(const auto&[id,w]:snapshot.windows)output.push_back({id,w.applied_x,w.applied_y,w.applied_width,w.applied_height,w.stacking,w.policy_visible,w.focused});return output;
}
}  // namespace glasswyrm::server
