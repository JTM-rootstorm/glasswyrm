#include "glasswyrmd/server_state.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace glasswyrm::server;
namespace {
void require(bool v,const char*m){if(!v){std::fprintf(stderr,"server lifecycle: %s\n",m);std::exit(1);}}
WindowCreateSpec spec(std::uint32_t id,std::uint32_t parent=1,WindowClass c=WindowClass::InputOutput){WindowCreateSpec s;s.xid=id;s.parent=parent;s.width=100;s.height=50;s.window_class=c;return s;}
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
 auto transaction=state.lifecycle_snapshot();auto& intent=transaction.windows.at(base+1);
 const auto before_map=top->map_requested;const auto before_serial=top->map_serial;
 intent.map_requested=!before_map;intent.map_serial=before_serial+100;intent.applied_width=0;
 require(!state.commit_lifecycle(transaction)&&state.resources().find_window(base+1)->map_requested==before_map&&state.resources().find_window(base+1)->map_serial==before_serial,"invalid lifecycle commit preserves intent atomically");
}
