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
}
int main(){
 constexpr std::uint32_t base=0x00400000,mask=0x001fffff;
 for(auto order:{x11::ByteOrder::LittleEndian,x11::ByteOrder::BigEndian}){
  ServerState state;DispatchContext context{1,base,mask,1,order};
  require(state.resources().create_window(1,base,mask,spec(base+1,1))==CreateWindowStatus::Success&&state.resources().create_window(1,base,mask,spec(base+2,base+1,WindowClass::InputOnly))==CreateWindowStatus::Success&&state.resources().create_window(1,base,mask,spec(base+3,base+1,WindowClass::InputOnly))==CreateWindowStatus::Success,"create hierarchy");
  auto result=dispatch_request(state,context,request(order,x11::CoreOpcode::MapWindow,base+2));
  require(result.output.empty()&&state.resources().find_window(base+2)->map_requested&&state.resources().find_window(base+2)->map_state==MapState::Unviewable,"local map decoded and applied");
  result=dispatch_request(state,context,configure(order,base+2,static_cast<std::uint16_t>(x11::ConfigureX|x11::ConfigureWidth|x11::ConfigureSibling|x11::ConfigureStackMode),{static_cast<std::uint32_t>(-7),80,base+3,0}));
  require(result.output.empty()&&state.resources().find_window(base+2)->x==-7&&state.resources().find_window(base+2)->width==80&&state.resources().find_window(base+1)->children.back()==base+2,"local configure and Above applied");
  result=dispatch_request(state,context,request(order,x11::CoreOpcode::UnmapWindow,base+2));
  require(result.output.empty()&&state.resources().find_window(base+2)->map_state==MapState::Unmapped,"local unmap applied");
  result=dispatch_request(state,context,request(order,x11::CoreOpcode::MapWindow,base+1));
  require(result.output.size()==32&&result.output[1]==static_cast<std::uint8_t>(x11::CoreErrorCode::BadImplementation),"top-level policy window remains explicit deferred boundary");
 }
}
