#include "glasswyrmd/request_dispatcher.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/core.hpp"
#include "protocol/x11/lifecycle_request.hpp"
#include <cstdio>
#include <cstdlib>
using namespace glasswyrm::server;
namespace x11=gw::protocol::x11;
namespace {
void require(bool v,const char*m){if(!v){std::fprintf(stderr,"local lifecycle dispatch: %s\n",m);std::exit(1);}}
WindowCreateSpec spec(std::uint32_t id,std::uint32_t parent,WindowClass c=WindowClass::InputOutput){WindowCreateSpec s;s.xid=id;s.parent=parent;s.width=100;s.height=50;s.window_class=c;return s;}
x11::FramedRequest request(x11::ByteOrder order,x11::CoreOpcode opcode,std::uint32_t window){x11::ByteWriter w(order);w.write_u8(static_cast<std::uint8_t>(opcode));w.write_u8(0);w.write_u16(2);w.write_u32(window);x11::FramedRequest r;r.opcode=static_cast<std::uint8_t>(opcode);r.length_units=2;r.bytes=std::move(w).take();return r;}
x11::FramedRequest configure(x11::ByteOrder order,std::uint32_t window,std::uint16_t mask,std::initializer_list<std::uint32_t> values){x11::ByteWriter w(order);w.write_u8(12);w.write_u8(0);w.write_u16(static_cast<std::uint16_t>(3+values.size()));w.write_u32(window);w.write_u16(mask);w.write_u16(0);for(auto v:values)w.write_u32(v);x11::FramedRequest r;r.opcode=12;r.length_units=static_cast<std::uint16_t>(3+values.size());r.bytes=std::move(w).take();return r;}
x11::FramedRequest create(x11::ByteOrder order,std::uint32_t window,std::uint32_t parent,WindowClass cls){x11::ByteWriter w(order);w.write_u8(1);w.write_u8(0);w.write_u16(8);w.write_u32(window);w.write_u32(parent);w.write_u16(0);w.write_u16(0);w.write_u16(20);w.write_u16(10);w.write_u16(0);w.write_u16(static_cast<std::uint16_t>(cls));w.write_u32(0);w.write_u32(0);x11::FramedRequest r;r.opcode=1;r.length_units=8;r.bytes=std::move(w).take();return r;}
x11::FramedRequest attributes(x11::ByteOrder order,std::uint32_t window,std::uint32_t mask,std::initializer_list<std::uint32_t> values){x11::ByteWriter w(order);w.write_u8(2);w.write_u8(0);w.write_u16(static_cast<std::uint16_t>(3+values.size()));w.write_u32(window);w.write_u32(mask);for(auto v:values)w.write_u32(v);x11::FramedRequest r;r.opcode=2;r.length_units=static_cast<std::uint16_t>(3+values.size());r.bytes=std::move(w).take();return r;}
}
int main(){
 constexpr std::uint32_t base=0x00400000,mask=0x001fffff;
 for(auto order:{x11::ByteOrder::LittleEndian,x11::ByteOrder::BigEndian}){
  ServerState state;DispatchContext context{1,base,mask,1,order};
  require(state.resources().create_window(1,base,mask,spec(base+1,1))==CreateWindowStatus::Success&&state.resources().create_window(1,base,mask,spec(base+2,base+1,WindowClass::InputOnly))==CreateWindowStatus::Success&&state.resources().create_window(1,base,mask,spec(base+3,base+1,WindowClass::InputOnly))==CreateWindowStatus::Success,"create hierarchy");
  require(state.resources().set_event_selection(base+2,1,1U<<17U)&&state.resources().set_event_selection(base+1,2,1U<<19U),"install local structural selectors");
  auto result=dispatch_request(state,context,request(order,x11::CoreOpcode::MapWindow,base+2));
  require(result.output.empty()&&result.structural_transitions.size()==1&&result.structural_transitions[0].before->structure_recipients==std::vector<ClientId>{1}&&result.structural_transitions[0].before->substructure_recipients==std::vector<ClientId>{2}&&state.resources().find_window(base+2)->map_requested&&state.resources().find_window(base+2)->map_state==MapState::Unviewable,"local map decoded, applied, and captured for both selectors");
  result=dispatch_request(state,context,request(order,x11::CoreOpcode::MapWindow,base+2));
  require(result.structural_transitions.size()==1&&result.structural_transitions[0].before->mapped&&result.structural_transitions[0].committed->mapped,"local no-op map is captured for router suppression");
  result=dispatch_request(state,context,configure(order,base+2,static_cast<std::uint16_t>(x11::ConfigureX|x11::ConfigureWidth|x11::ConfigureSibling|x11::ConfigureStackMode),{static_cast<std::uint32_t>(-7),80,base+3,0}));
  require(result.output.empty()&&result.structural_transitions.size()==1&&state.resources().find_window(base+2)->x==-7&&state.resources().find_window(base+2)->width==80&&state.resources().find_window(base+1)->children.back()==base+2,"local configure and Above applied with transition");
  result=dispatch_request(state,context,request(order,x11::CoreOpcode::UnmapWindow,base+2));
  require(result.output.empty()&&result.structural_transitions.size()==1&&result.structural_transitions[0].before->mapped&&!result.structural_transitions[0].committed->mapped&&state.resources().find_window(base+2)->map_state==MapState::Unmapped,"local unmap applied with transition");
  result=dispatch_request(state,context,request(order,x11::CoreOpcode::DestroyWindow,base+2));
  require(result.structural_transitions.size()==1&&result.structural_transitions[0].kind==StructuralTransitionKind::Destroy&&result.structural_transitions[0].before->structure_recipients==std::vector<ClientId>{1}&&!result.structural_transitions[0].committed&&state.resources().find_window(base+2)==nullptr,"local destroy captures recipients before mutation");
  result=dispatch_request(state,context,request(order,x11::CoreOpcode::MapWindow,base+1));
  require(result.output.size()==32&&result.output[1]==static_cast<std::uint8_t>(x11::CoreErrorCode::BadImplementation),"top-level policy window remains explicit deferred boundary");
  context.integrated_lifecycle=true;
  result=dispatch_request(state,context,create(order,base+20,1,WindowClass::InputOutput));
  require(result.kind==DispatchKind::DeferredLifecycle&&result.deferred_create&&result.deferred_window==base+20&&!state.resources().find_window(base+20),"integrated top-level CreateWindow is staged with decoded payload");
  result=dispatch_request(state,context,create(order,base+21,base+1,WindowClass::InputOnly));
  require(result.kind==DispatchKind::Immediate&&state.resources().find_window(base+21),"integrated InputOnly child remains synchronous");
  state.resources().find_window(base+1)->map_requested=true;
  result=dispatch_request(state,context,attributes(order,base+1,(1U<<1U)|(1U<<9U),{0x12345678U,1}));
  require(result.kind==DispatchKind::DeferredLifecycle&&result.deferred_override_redirect==true&&result.deferred_window==base+1&&!state.resources().find_window(base+1)->attributes.override_redirect&&state.resources().find_window(base+1)->attributes.background_pixel==0x00345678U&&state.resources().find_window(base+1)->attributes.background_source==BackgroundSource::Pixel,"mapped top-level override change defers while other validated attributes apply synchronously");
  require(state.resources().create_window(1,base,mask,spec(base+30,base+1))==CreateWindowStatus::Success&&state.resources().create_window(1,base,mask,spec(base+31,base+30))==CreateWindowStatus::Success&&state.resources().create_window(1,base,mask,spec(base+32,base+30))==CreateWindowStatus::Success,"create bulk lifecycle hierarchy");
  state.resources().find_window(base+30)->map_requested=true; state.resources().find_window(base+30)->map_state=MapState::Viewable;
  result=dispatch_request(state,context,request(order,x11::CoreOpcode::MapSubwindows,base+30));
  require(result.output.empty()&&result.structural_transitions.size()==2&&result.structural_transitions[0].committed->target==base+31&&result.structural_transitions[1].committed->target==base+32&&result.drawable_damage.size()==2&&state.resources().find_window(base+31)->map_state==MapState::Viewable&&state.resources().find_window(base+32)->map_state==MapState::Viewable,"MapSubwindows maps immediate children bottom-to-top with presentation damage");
  result=dispatch_request(state,context,request(order,x11::CoreOpcode::UnmapSubwindows,base+30));
  require(result.output.empty()&&result.structural_transitions.size()==2&&result.structural_transitions[0].before->target==base+31&&result.structural_transitions[1].before->target==base+32&&result.drawable_damage.size()==2&&result.expose_intents.size()==2&&result.expose_intents[0].window==base+30&&!state.resources().find_window(base+31)->map_requested&&!state.resources().find_window(base+32)->map_requested,"UnmapSubwindows preserves order and reveals parent");
  result=dispatch_request(state,context,request(order,x11::CoreOpcode::MapWindow,base+31));
  require(result.drawable_damage.size()==1,"individual child map damages top-level presentation");
  result=dispatch_request(state,context,configure(order,base+31,x11::ConfigureWidth,{80}));
  require(result.structural_transitions.size()==1&&result.drawable_damage.size()==2,"child configure damages old and new presentation bounds");
  result=dispatch_request(state,context,request(order,x11::CoreOpcode::DestroyWindow,base+31));
  require(result.structural_transitions.size()==1&&result.drawable_damage.size()==1&&result.expose_intents.size()==1&&result.expose_intents[0].window==base+30,"viewable child destroy damages presentation and exposes parent");
  result=dispatch_request(state,context,request(order,x11::CoreOpcode::DestroyWindow,base+1));
  require(result.kind==DispatchKind::DeferredLifecycle&&result.deferred_destroy&&state.resources().find_window(base+1),"integrated top-level DestroyWindow is staged without early mutation");
 }
}
