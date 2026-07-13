#include "glasswyrmd/request_dispatcher.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/core.hpp"

#include <cstdio>
#include <cstdlib>
#include <span>
#include <string_view>

using namespace glasswyrm::server;
namespace x11 = gw::protocol::x11;

namespace {
void require(bool value, const char* message) { if (!value) { std::fprintf(stderr, "compatibility queries: %s\n", message); std::exit(1); } }

x11::FramedRequest finish(x11::ByteWriter writer, x11::CoreOpcode opcode, std::uint8_t data = 0) {
  auto bytes = std::move(writer).take();
  x11::FramedRequest request; request.opcode = static_cast<std::uint8_t>(opcode); request.data = data;
  request.length_units = static_cast<std::uint16_t>(bytes.size() / 4); request.bytes = std::move(bytes); return request;
}
x11::ByteWriter header(x11::ByteOrder order, x11::CoreOpcode opcode, std::uint16_t units) { x11::ByteWriter writer(order); writer.write_u8(static_cast<std::uint8_t>(opcode)); writer.write_u8(0); writer.write_u16(units); return writer; }
std::uint16_t u16(std::span<const std::uint8_t> bytes, x11::ByteOrder order, std::size_t offset) { x11::ByteReader reader(bytes.subspan(offset), order); std::uint16_t value{}; require(reader.read_u16(value), "read u16"); return value; }
std::uint32_t u32(std::span<const std::uint8_t> bytes, x11::ByteOrder order, std::size_t offset) { x11::ByteReader reader(bytes.subspan(offset), order); std::uint32_t value{}; require(reader.read_u32(value), "read u32"); return value; }

x11::FramedRequest named(x11::ByteOrder order, x11::CoreOpcode opcode, std::uint32_t cmap, std::string_view name) {
  const auto padded = (name.size() + 3U) & ~std::size_t{3U}; auto writer = header(order, opcode, static_cast<std::uint16_t>(3 + padded / 4)); writer.write_u32(cmap); writer.write_u16(static_cast<std::uint16_t>(name.size())); writer.write_u16(0); writer.write_bytes(std::span(reinterpret_cast<const std::uint8_t*>(name.data()), name.size())); writer.write_padding(padded-name.size()); return finish(std::move(writer), opcode);
}
}

int main() {
  for (const auto order : {x11::ByteOrder::LittleEndian, x11::ByteOrder::BigEndian}) {
    ServerState state; DispatchContext context{1, 0x400000, 0x1fffff, 0x12345, order}; const auto cmap = state.screen().default_colormap;
    auto writer = header(order, x11::CoreOpcode::QueryExtension, 4); writer.write_u16(6); writer.write_u16(0); writer.write_bytes(std::span(reinterpret_cast<const std::uint8_t*>("RENDER"), 6)); writer.write_padding(2);
    auto result = dispatch_request(state, context, finish(std::move(writer), x11::CoreOpcode::QueryExtension));
    require(result.output.size()==32 && result.output[1]==0 && u32(result.output,order,4)==0, "QueryExtension absent reply");
    result = dispatch_request(state, context, finish(header(order,x11::CoreOpcode::ListExtensions,1),x11::CoreOpcode::ListExtensions)); require(result.output.size()==32 && result.output[1]==0, "ListExtensions empty reply");

    writer=header(order,x11::CoreOpcode::AllocColor,4); writer.write_u32(cmap); writer.write_u16(0x12ff); writer.write_u16(0x8001); writer.write_u16(0xffff); writer.write_u16(0);
    result=dispatch_request(state,context,finish(std::move(writer),x11::CoreOpcode::AllocColor)); require(u16(result.output,order,8)==0x1212 && u16(result.output,order,10)==0x8080 && u32(result.output,order,16)==0x1280ff,"AllocColor quantizes TrueColor");
    result=dispatch_request(state,context,named(order,x11::CoreOpcode::AllocNamedColor,cmap,"Light Grey")); require(u32(result.output,order,8)==0xd3d3d3 && u16(result.output,order,12)==0xd3d3,"AllocNamedColor alias");
    result=dispatch_request(state,context,named(order,x11::CoreOpcode::LookupColor,cmap,"#f08")); require(u16(result.output,order,8)==0xffff && u16(result.output,order,10)==0 && u16(result.output,order,12)==0x8888,"LookupColor numeric");
    result=dispatch_request(state,context,named(order,x11::CoreOpcode::LookupColor,cmap,"unknown")); require(result.output[0]==0 && result.output[1]==static_cast<std::uint8_t>(x11::CoreErrorCode::BadName),"unknown color BadName");
    result=dispatch_request(state,context,named(order,x11::CoreOpcode::LookupColor,cmap+1,"red")); require(result.output[1]==static_cast<std::uint8_t>(x11::CoreErrorCode::BadColormap),"invalid colormap");
    writer=header(order,x11::CoreOpcode::QueryColors,4); writer.write_u32(cmap); writer.write_u32(0x123456); writer.write_u32(0xff0080); result=dispatch_request(state,context,finish(std::move(writer),x11::CoreOpcode::QueryColors)); require(u16(result.output,order,8)==2 && u16(result.output,order,32)==0x1212 && u16(result.output,order,44)==0x8080,"QueryColors components");
    writer=header(order,x11::CoreOpcode::FreeColors,4); writer.write_u32(cmap); writer.write_u32(0xffffffff); writer.write_u32(0x123456); result=dispatch_request(state,context,finish(std::move(writer),x11::CoreOpcode::FreeColors)); require(result.output.empty(),"FreeColors no-op");

    writer=header(order,x11::CoreOpcode::GetKeyboardMapping,2); writer.write_u8(9); writer.write_u8(2); writer.write_u16(0); result=dispatch_request(state,context,finish(std::move(writer),x11::CoreOpcode::GetKeyboardMapping)); require(result.output[1]==2 && u32(result.output,order,32)==0xff1b && u32(result.output,order,40)==0x31 && u32(result.output,order,44)==0x21,"keyboard mapping");
    result=dispatch_request(state,context,finish(header(order,x11::CoreOpcode::GetPointerMapping,1),x11::CoreOpcode::GetPointerMapping)); require(result.output[1]==5 && result.output[32]==1 && result.output[36]==5,"pointer mapping");
    result=dispatch_request(state,context,finish(header(order,x11::CoreOpcode::GetModifierMapping,1),x11::CoreOpcode::GetModifierMapping)); require(result.output[1]==2 && result.output[32]==50 && result.output[33]==62 && result.output[36]==37 && result.output[38]==64,"modifier mapping");

    WindowCreateSpec top; top.xid=0x400001; top.parent=state.screen().root_window; top.x=100; top.y=50; top.width=200; top.height=150; top.border_width=2; top.window_class=WindowClass::InputOutput;
    WindowCreateSpec child; child.xid=0x400002; child.parent=top.xid; child.x=10; child.y=20; child.width=50; child.height=40; child.border_width=1; child.window_class=WindowClass::InputOutput;
    require(state.resources().create_window(1,0x400000,0x1fffff,top)==CreateWindowStatus::Success && state.resources().create_window(1,0x400000,0x1fffff,child)==CreateWindowStatus::Success,"create coordinate hierarchy");
    state.resources().find_window(top.xid)->map_state=MapState::Viewable; state.resources().find_window(child.xid)->map_state=MapState::Viewable;
    context.input={115,75,0x0105,child.xid,77};
    writer=header(order,x11::CoreOpcode::QueryPointer,2); writer.write_u32(top.xid); result=dispatch_request(state,context,finish(std::move(writer),x11::CoreOpcode::QueryPointer)); require(result.output[1]==1 && u32(result.output,order,8)==state.screen().root_window && u32(result.output,order,12)==child.xid && static_cast<std::int16_t>(u16(result.output,order,20))==13 && static_cast<std::int16_t>(u16(result.output,order,22))==23 && u16(result.output,order,24)==0x0105,"QueryPointer child and relative coordinates");
    writer=header(order,x11::CoreOpcode::TranslateCoordinates,4); writer.write_u32(child.xid); writer.write_u32(top.xid); writer.write_u16(static_cast<std::uint16_t>(-1)); writer.write_u16(2); result=dispatch_request(state,context,finish(std::move(writer),x11::CoreOpcode::TranslateCoordinates)); require(result.output[1]==1 && u32(result.output,order,8)==child.xid && static_cast<std::int16_t>(u16(result.output,order,12))==10 && static_cast<std::int16_t>(u16(result.output,order,14))==23,"TranslateCoordinates nested signed point");
    writer=header(order,x11::CoreOpcode::QueryPointer,2); writer.write_u32(0x4fffff); result=dispatch_request(state,context,finish(std::move(writer),x11::CoreOpcode::QueryPointer)); require(result.output[1]==static_cast<std::uint8_t>(x11::CoreErrorCode::BadWindow),"QueryPointer BadWindow");
  }
}
