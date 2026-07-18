#include "glasswyrmd/lifecycle_projection.hpp"

#include <algorithm>

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

}  // namespace

PolicySnapshotSubmission project_policy(const LifecycleSnapshot& snapshot,
                                        std::uint64_t commit,
                                        std::uint64_t generation) {
  PolicySnapshotSubmission output{commit, generation, {}};
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
  }
  return output;
}

std::optional<LifecycleSnapshot> apply_policy_result(
    const LifecycleSnapshot& proposed, const PolicySnapshotResult& result) {
  if (result.windows.size()!=proposed.windows.size()) return std::nullopt;
  auto evaluated=proposed; evaluated.focused_window=proposed.root_window;
  std::vector<std::pair<std::int32_t,std::uint32_t>> visible;
  for(const auto& state:result.windows){
    auto found=evaluated.windows.find(state.window_id); if(found==evaluated.windows.end())return std::nullopt;
    if(state.workspace_id!=proposed.workspace_id||state.output_id!=proposed.output_id||
       state.final_width==0||state.final_height==0||state.final_width>UINT16_MAX||state.final_height>UINT16_MAX||
       state.final_x<INT16_MIN||state.final_x>INT16_MAX||state.final_y<INT16_MIN||state.final_y>INT16_MAX||
       state.window_type>GWIPC_POLICY_WINDOW_UTILITY||state.applied_state<GWIPC_POLICY_APPLIED_NORMAL||state.applied_state>GWIPC_POLICY_APPLIED_MINIMIZED||
       state.visible>1||state.focused>1||state.managed>1||state.decoration_eligible>1||state.override_redirect>1||state.attention_requested>1||
       state.fullscreen_eligible>GWIPC_TRI_STATE_TRUE||state.direct_scanout_eligible>GWIPC_TRI_STATE_TRUE)return std::nullopt;
    auto& window=found->second; window.applied_x=state.final_x;window.applied_y=state.final_y;
    window.applied_width=state.final_width;window.applied_height=state.final_height;
    window.stacking=state.stacking;window.policy_visible=state.visible;window.focused=state.focused;
    window.window_type=static_cast<std::uint8_t>(state.window_type);window.applied_state=static_cast<std::uint8_t>(state.applied_state);
    window.managed=state.managed;window.decoration_eligible=state.decoration_eligible;window.override_redirect=state.override_redirect;
    window.attention_requested=state.attention_requested;window.fullscreen_eligible=static_cast<std::uint8_t>(state.fullscreen_eligible);
    window.direct_scanout_eligible=static_cast<std::uint8_t>(state.direct_scanout_eligible);
    if(state.focused){if(evaluated.focused_window!=proposed.root_window||!state.visible||!state.managed)return std::nullopt;evaluated.focused_window=state.window_id;}
    if(state.visible)visible.emplace_back(state.stacking,state.window_id);else if(state.stacking!=-1)return std::nullopt;
  }
  std::sort(visible.begin(),visible.end());
  for(std::size_t i=0;i<visible.size();++i)if(visible[i].first!=static_cast<std::int32_t>(i))return std::nullopt;
  std::vector<std::uint32_t> order;
  for(const auto id:proposed.root_order)
    if(std::none_of(visible.begin(),visible.end(),[&](const auto& item){return item.second==id;}))order.push_back(id);
  for(auto [stack,id]:visible){(void)stack;order.push_back(id);}
  evaluated.root_order=std::move(order);return evaluated;
}

CompositorSnapshotSubmission project_compositor(const LifecycleSnapshot& snapshot,
                                                std::uint64_t commit,
                                                std::uint64_t generation,
                                                const bool software_content) {
  CompositorSnapshotSubmission output{commit,generation,{},{},{},{}};
  for(const auto& [id,value]:snapshot.windows){
    gwipc_surface_upsert surface{};surface.struct_size=sizeof(surface);surface.surface_id=(UINT64_C(1)<<32)|id;
    surface.x11_window_id=id;surface.output_id=snapshot.output_id;surface.logical_x=value.applied_x;surface.logical_y=value.applied_y;
    surface.logical_width=value.applied_width;surface.logical_height=value.applied_height;surface.stacking=value.stacking;
    surface.visible=value.policy_visible;surface.transform=GWIPC_TRANSFORM_NORMAL;surface.opacity=GWIPC_OPACITY_ONE;
    surface.scale_numerator=surface.scale_denominator=1;surface.color={GWIPC_SDR_COLOR_SPACE_SRGB,GWIPC_TRANSFER_FUNCTION_SRGB,GWIPC_COLOR_PRIMARIES_SRGB,0,0,0,0};
    surface.presentation_flags=software_content ? 0U : GWIPC_SURFACE_PRESENTATION_METADATA_ONLY;output.surfaces.push_back(surface);
    gwipc_surface_policy_upsert policy{};policy.struct_size=sizeof(policy);policy.surface_id=surface.surface_id;
    policy.x11_window_id=id;policy.workspace_id=snapshot.workspace_id;policy.window_type=static_cast<gwipc_policy_window_type>(value.window_type);
    policy.applied_state=static_cast<gwipc_policy_applied_state>(value.applied_state);policy.focused=value.focused;policy.managed=value.managed;
    policy.decoration_eligible=value.decoration_eligible;policy.override_redirect=value.override_redirect;policy.attention_requested=value.attention_requested;
    policy.fullscreen_eligible=static_cast<gwipc_tri_state>(value.fullscreen_eligible);policy.direct_scanout_eligible=static_cast<gwipc_tri_state>(value.direct_scanout_eligible);
    output.policies.push_back(policy);
  }return output;
}

std::vector<AppliedPolicyWindow> applied_policy(const LifecycleSnapshot& snapshot){
 std::vector<AppliedPolicyWindow> output;for(const auto&[id,w]:snapshot.windows)output.push_back({id,w.applied_x,w.applied_y,w.applied_width,w.applied_height,w.stacking,w.policy_visible,w.focused});return output;
}
}  // namespace glasswyrm::server
