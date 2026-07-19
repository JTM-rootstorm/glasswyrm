#include "glasswyrmd/server_state.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>
using namespace glasswyrm::server;
namespace {
void require(bool v,const char*m){if(!v){std::fprintf(stderr,"server lifecycle: %s\n",m);std::exit(1);}}
WindowCreateSpec spec(std::uint32_t id,std::uint32_t parent=1,WindowClass c=WindowClass::InputOutput){WindowCreateSpec s;s.xid=id;s.parent=parent;s.width=100;s.height=50;s.window_class=c;return s;}
glasswyrm::output::OutputLayout output_layout(){
 using namespace glasswyrm::output;
 constexpr OutputId output_id{10};constexpr OutputModeId mode_id{11};
 OutputDescriptor descriptor;descriptor.id=output_id;descriptor.name="SCALE";descriptor.connected=true;descriptor.mode_configurable=true;descriptor.scale_configurable=true;descriptor.primary_eligible=true;descriptor.arbitrary_headless_mode=true;
 descriptor.modes.push_back({mode_id,output_id,800,600,60'000,0,"800x600",true,true});
 OutputState output;output.output_id=output_id;output.enabled=true;output.mode_id=mode_id;output.logical_width=640;output.logical_height=480;output.physical_width=800;output.physical_height=600;output.refresh_millihertz=60'000;output.scale={5,4};output.primary=true;output.generation=7;
 OutputLayout layout;layout.descriptors.emplace(output_id,std::move(descriptor));layout.states.emplace(output_id,output);layout.primary_output_id=output_id;layout.root_logical_width=640;layout.root_logical_height=480;layout.generation=7;layout.enabled_output_count=1;layout.output_order={output_id};return layout;
}
}
int main(){
 LifecycleSerialSource source(std::numeric_limits<std::uint64_t>::max());
 require(source.take()==std::numeric_limits<std::uint64_t>::max()&&!source.take()&&source.exhausted(),"serial never wraps");
 constexpr std::uint32_t base=0x00400000,mask=0x001fffff; ServerState state;
 require(state.resources().create_window(1,base,mask,spec(base+1))==CreateWindowStatus::Success&&state.resources().create_window(1,base,mask,spec(base+2,base+1,WindowClass::InputOnly))==CreateWindowStatus::Success,"create windows");
 require(state.lifecycle_snapshot().windows.size()==1,"snapshot filters candidates");
 require(state.resources().set_local_map_intent(base+2,true)==LocalLifecycleStatus::Success&&state.resources().find_window(base+2)->map_state==MapState::Unviewable,"child unviewable");
 auto* top=state.resources().find_window(base+1);top->map_requested=true;top->map_serial=*state.next_lifecycle_serial();
 const AppliedPolicyWindow applied{base+1,40,30,200,120,0,true,true};
 require(state.apply_policy(std::span<const AppliedPolicyWindow>(&applied,1))&&top->map_state==MapState::Viewable&&state.resources().find_window(base+2)->map_state==MapState::Viewable&&top->requested_x==40&&top->geometry_serial!=0&&state.focused_window()==base+1,"policy promotion");
 LocalConfigure config;config.x=-5;config.width=80;
 require(state.resources().configure_local(base+2,config)==LocalLifecycleStatus::Success&&state.resources().find_window(base+2)->x==-5&&state.resources().find_window(base+2)->requested_width==80,"local configure");
 const auto before=top->x;const AppliedPolicyWindow invalid{base+1,99,0,0,10,0,true,false};
 require(!state.apply_policy(std::span<const AppliedPolicyWindow>(&invalid,1))&&top->x==before,"invalid atomic");
 auto override_transaction=state.lifecycle_snapshot();override_transaction.windows.at(base+1).override_redirect=true;override_transaction.windows.at(base+1).stacking=0;override_transaction.windows.at(base+1).focus_serial=73;
 override_transaction.windows.at(base+1).applied_state = 3;
 override_transaction.windows.at(base+1).managed = true;
 override_transaction.windows.at(base+1).decoration_eligible = false;
 override_transaction.windows.at(base+1).fullscreen_eligible = 2;
 override_transaction.windows.at(base+1).direct_scanout_eligible = 1;
 require(state.commit_lifecycle(override_transaction)&&state.resources().find_window(base+1)->attributes.override_redirect&&state.resources().find_window(base+1)->focus_serial==73,"accepted lifecycle commit persists override-redirect and focus serial atomically");
 const auto committed_policy = state.lifecycle_snapshot().windows.at(base+1);
 require(committed_policy.applied_state == 3 && committed_policy.managed &&
             !committed_policy.decoration_eligible &&
             committed_policy.fullscreen_eligible == 2 &&
             committed_policy.direct_scanout_eligible == 1,
         "accepted lifecycle commit persists applied window policy fields");
 require(state.randr().configure_output_layout(output_layout()),"configure lifecycle scale output topology");
 top=state.resources().find_window(base+1);top->scale.accepted_buffer_scale=2;top->scale.presentation=WindowScalePresentationState::ScaleAwareActive;top->scale.event_selections.emplace(123,7);
 auto scale_transaction=state.lifecycle_snapshot();auto& staged_scale=scale_transaction.windows.at(base+1);staged_scale.assigned_output_id=10;staged_scale.output_memberships={10};staged_scale.scale.has_output_state=true;staged_scale.scale.preferred_scale_numerator=5;staged_scale.scale.preferred_scale_denominator=4;staged_scale.scale.layout_generation=7;
 require(top->scale.primary_output==0&&!top->scale.has_output_state,"evaluated lifecycle output state does not mutate the live window early");
 require(state.commit_lifecycle(scale_transaction),"accepted compositor lifecycle commits staged scale state");
 top=state.resources().find_window(base+1);
 require(top->scale.has_output_state&&top->scale.primary_output==state.randr().primary_output_xid()&&top->scale.output_memberships==std::vector<std::uint32_t>({state.randr().primary_output_xid()})&&top->scale.preferred_scale_numerator==5&&top->scale.preferred_scale_denominator==4&&top->scale.layout_generation==7&&top->scale.accepted_buffer_scale==2&&top->scale.presentation==WindowScalePresentationState::ScaleAwareActive&&top->scale.event_selections.at(123)==7,"lifecycle commit maps internal outputs while preserving client scale state");
 auto persisted_scale_transaction=state.lifecycle_snapshot();const auto& persisted_scale=persisted_scale_transaction.windows.at(base+1);
 require(persisted_scale.assigned_output_id==10&&persisted_scale.output_memberships==std::vector<std::uint64_t>({10}),"next lifecycle snapshot restores internal primary and membership IDs");
 require(state.commit_lifecycle(persisted_scale_transaction)&&state.lifecycle_snapshot().windows.at(base+1).assigned_output_id==10&&state.lifecycle_snapshot().windows.at(base+1).output_memberships==std::vector<std::uint64_t>({10}),"second same-layout lifecycle round-trip preserves output hint inputs");
 top=state.resources().find_window(base+1);
 auto transaction=state.lifecycle_snapshot();auto& intent=transaction.windows.at(base+1);
 const auto before_map=top->map_requested;const auto before_serial=top->map_serial;
 intent.map_requested=!before_map;intent.map_serial=before_serial+100;intent.applied_width=0;
 require(!state.commit_lifecycle(transaction)&&state.resources().find_window(base+1)->map_requested==before_map&&state.resources().find_window(base+1)->map_serial==before_serial,"invalid lifecycle commit preserves intent atomically");
 ServerState staged_state;const auto create_spec=spec(base+10);
 auto proposed=staged_state.propose_create_lifecycle(2,base,mask,create_spec,41);
 require(proposed&&proposed->windows.contains(base+10)&&!staged_state.resources().find_window(base+10),"create proposal is visible to policy without early resource mutation");
 require(staged_state.commit_create_lifecycle(2,base,mask,create_spec,41,*proposed)&&staged_state.resources().find_window(base+10)->creation_serial==41,"accepted create commits resource and serial atomically");
 const auto primary=staged_state.atoms().intern("PRIMARY_M11",false).atom;
 require(staged_state.selections().set_owner(2,primary,base+10,true,0,50).status==SelectionOwnershipStatus::Applied,"lifecycle window can own a selection");
 auto destroyed=staged_state.propose_destroy_lifecycle(base+10);
 require(destroyed&&!destroyed->windows.contains(base+10)&&staged_state.resources().find_window(base+10),"destroy proposal removes policy window without early resource mutation");
 require(staged_state.commit_destroy_lifecycle(base+10,*destroyed)&&!staged_state.resources().find_window(base+10)&&!staged_state.selections().owner(primary),"accepted destroy commits resource and selection cleanup atomically");
}
