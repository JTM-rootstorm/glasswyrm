#include "glasswyrmd/lifecycle_projection.hpp"
#include <cstdio>
#include <cstdlib>
using namespace glasswyrm::server;
namespace { void require(bool v,const char*m){if(!v){std::fprintf(stderr,"lifecycle projection: %s\n",m);std::exit(1);}} }
int main(){
 LifecycleSnapshot proposed;proposed.root_window=77;proposed.workspace_id=3;proposed.output_id=9;proposed.focused_window=77;proposed.root_order={999,10,20};
 LifecycleWindow a;a.xid=10;a.parent=77;a.window_class=WindowClass::InputOutput;a.requested_width=100;a.requested_height=50;a.creation_serial=1;
 a.transient_for=9;a.policy_window_type=PolicyWindowType::Utility;a.decoration_preference=PolicyDecoration::False;
 a.above_requested=true;a.bypass_compositor=true;a.input_requested=false;a.attention_requested=true;
 a.minimum_width=120;a.maximum_width=150;a.minimum_height=40;a.maximum_height=45;
 LifecycleWindow b=a;b.xid=20;b.creation_serial=2;proposed.windows.emplace(10,a);proposed.windows.emplace(20,b);
 PolicySnapshotResult result;result.generation=4;
 gwipc_policy_window_state hidden{};hidden.struct_size=sizeof(hidden);hidden.window_id=10;hidden.workspace_id=3;hidden.output_id=9;hidden.final_width=100;hidden.final_height=50;hidden.stacking=-1;hidden.window_type=GWIPC_POLICY_WINDOW_DIALOG;hidden.applied_state=GWIPC_POLICY_APPLIED_MINIMIZED;hidden.managed=1;hidden.decoration_eligible=1;hidden.fullscreen_eligible=GWIPC_TRI_STATE_FALSE;hidden.direct_scanout_eligible=GWIPC_TRI_STATE_UNKNOWN;
 auto visible=hidden;visible.window_id=20;visible.final_x=12;visible.final_y=14;visible.stacking=0;visible.visible=1;visible.focused=1;visible.window_type=GWIPC_POLICY_WINDOW_UTILITY;visible.applied_state=GWIPC_POLICY_APPLIED_FULLSCREEN;visible.attention_requested=1;visible.fullscreen_eligible=GWIPC_TRI_STATE_TRUE;
 result.windows={visible,hidden};auto evaluated=apply_policy_result(proposed,result);
 require(evaluated&&evaluated->focused_window==20&&evaluated->root_order==std::vector<std::uint32_t>({999,10,20}),"focus and hidden/nonpolicy root order retained");
 const auto& stored=evaluated->windows.at(20);require(stored.window_type==GWIPC_POLICY_WINDOW_UTILITY&&stored.applied_state==GWIPC_POLICY_APPLIED_FULLSCREEN&&stored.managed&&stored.decoration_eligible&&stored.attention_requested&&stored.fullscreen_eligible==GWIPC_TRI_STATE_TRUE,"exact returned metadata retained");
 auto compositor=project_compositor(*evaluated,5,4);require(compositor.surfaces.size()==2&&compositor.policies.size()==2&&compositor.surfaces.at(1).output_id==9&&compositor.policies.at(1).workspace_id==3&&compositor.policies.at(1).window_type==GWIPC_POLICY_WINDOW_UTILITY&&compositor.policies.at(1).applied_state==GWIPC_POLICY_APPLIED_FULLSCREEN&&compositor.policies.at(1).attention_requested==1,"exact metadata projected to compositor");
 auto buffered=project_compositor(*evaluated,6,5,true);require(buffered.surfaces.at(1).presentation_flags==0,"software content projects buffered surfaces");
 result.windows.at(0).final_width=UINT32_MAX;require(!apply_policy_result(proposed,result),"unrepresentable geometry rejected before compositor");
 result.windows.at(0)=visible;result.windows.at(0).visible=2;require(!apply_policy_result(proposed,result),"invalid boolean rejected");
 auto policy=project_policy(proposed,8,9);require(policy.windows.size()==2&&policy.windows.at(0).window.parent_window_id==77&&policy.windows.at(0).window.workspace_id==3,"screen-derived root/workspace projected");
 const auto& wire=policy.windows.at(0).window;
 require(wire.transient_for==9&&wire.window_type==GWIPC_POLICY_WINDOW_UTILITY&&wire.decoration_preference==GWIPC_TRI_STATE_FALSE&&wire.attention_requested==1&&wire.requested_width==120&&wire.requested_height==45&&wire.flags==(GWIPC_POLICY_WINDOW_FLAG_ABOVE|GWIPC_POLICY_WINDOW_FLAG_BYPASS_COMPOSITOR|GWIPC_POLICY_WINDOW_FLAG_INPUT_DISABLED),"EWMH type transient decoration attention size bounds and policy flags project to GWM wire");
}
