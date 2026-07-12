#include "glasswyrmd/request_dispatcher.hpp"
#include "protocol/x11/byte_cursor.hpp"
#include "protocol/x11/core.hpp"
#include "helpers/test_support.hpp"

namespace x11 = gw::protocol::x11;
using namespace glasswyrm::server;

static x11::FramedRequest finish(x11::ByteWriter writer, x11::CoreOpcode opcode,
                                 std::uint8_t data) {
  x11::FramedRequest result; result.opcode=static_cast<std::uint8_t>(opcode);
  result.data=data; result.bytes=std::move(writer).take();
  result.length_units=static_cast<std::uint16_t>(result.bytes.size()/4); return result;
}

int main() {
  constexpr std::uint32_t base=0x00400000U, mask=0x001fffffU;
  for (const auto order : {x11::ByteOrder::LittleEndian, x11::ByteOrder::BigEndian}) {
    ServerState state; DispatchContext context{1,base,mask,9,order};
    x11::ByteWriter pixmap(order); pixmap.write_u8(53); pixmap.write_u8(24); pixmap.write_u16(4);
    pixmap.write_u32(base+1); pixmap.write_u32(state.screen().root_window); pixmap.write_u16(2); pixmap.write_u16(2);
    auto result=dispatch_request(state,context,finish(std::move(pixmap),x11::CoreOpcode::CreatePixmap,24));
    gw::test::require(result.output.empty()&&state.resources().find_pixmap(base+1),"CreatePixmap");
    x11::ByteWriter gc(order); gc.write_u8(55); gc.write_u8(0); gc.write_u16(5); gc.write_u32(base+2); gc.write_u32(base+1); gc.write_u32(1U<<2U); gc.write_u32(0x00112233U);
    result=dispatch_request(state,context,finish(std::move(gc),x11::CoreOpcode::CreateGC,0));
    gw::test::require(result.output.empty()&&state.resources().find_gc(base+2),"CreateGC");
    x11::ByteWriter fill(order); fill.write_u8(70); fill.write_u8(0); fill.write_u16(5); fill.write_u32(base+1); fill.write_u32(base+2); fill.write_u16(0); fill.write_u16(0); fill.write_u16(2); fill.write_u16(2);
    result=dispatch_request(state,context,finish(std::move(fill),x11::CoreOpcode::PolyFillRectangle,0));
    gw::test::require(result.output.empty()&&state.resources().find_pixmap(base+1)->storage->at(1,1)==0xff112233U,"PolyFillRectangle");
    x11::ByteWriter geometry(order); geometry.write_u8(14); geometry.write_u8(0); geometry.write_u16(2); geometry.write_u32(base+1);
    result=dispatch_request(state,context,finish(std::move(geometry),x11::CoreOpcode::GetGeometry,0));
    gw::test::require(result.output.size()==32&&result.output[1]==24,"GetGeometry pixmap");
    x11::ByteWriter change(order); change.write_u8(56); change.write_u8(0); change.write_u16(4); change.write_u32(base+2); change.write_u32(1U<<0U); change.write_u32(6);
    result=dispatch_request(state,context,finish(std::move(change),x11::CoreOpcode::ChangeGC,0));
    gw::test::require(result.output.size()==32&&result.output[1]==static_cast<std::uint8_t>(x11::CoreErrorCode::BadValue)&&state.resources().find_gc(base+2)->function==3,"atomic ChangeGC");
  }
}
