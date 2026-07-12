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
    x11::ByteWriter gc(order); gc.write_u8(55); gc.write_u8(0); gc.write_u16(7); gc.write_u32(base+2); gc.write_u32(base+1); gc.write_u32((1U<<1U)|(1U<<2U)|(1U<<16U)); gc.write_u32(0x00ffffffU); gc.write_u32(0x00112233U); gc.write_u32(1);
    result=dispatch_request(state,context,finish(std::move(gc),x11::CoreOpcode::CreateGC,0));
    gw::test::require(result.output.empty()&&state.resources().find_gc(base+2)&&
        state.resources().find_gc(base+2)->plane_mask==0x00ffffffU&&
        state.resources().find_gc(base+2)->foreground==0x00112233U&&
        state.resources().find_gc(base+2)->graphics_exposures,"CreateGC ordered values");
    x11::ByteWriter fill(order); fill.write_u8(70); fill.write_u8(0); fill.write_u16(5); fill.write_u32(base+1); fill.write_u32(base+2); fill.write_u16(0); fill.write_u16(0); fill.write_u16(2); fill.write_u16(2);
    result=dispatch_request(state,context,finish(std::move(fill),x11::CoreOpcode::PolyFillRectangle,0));
    gw::test::require(result.output.empty()&&state.resources().find_pixmap(base+1)->storage->at(1,1)==0xff112233U,"PolyFillRectangle");
    x11::ByteWriter geometry(order); geometry.write_u8(14); geometry.write_u8(0); geometry.write_u16(2); geometry.write_u32(base+1);
    result=dispatch_request(state,context,finish(std::move(geometry),x11::CoreOpcode::GetGeometry,0));
    gw::test::require(result.output.size()==32&&result.output[1]==24,"GetGeometry pixmap");
    x11::ByteWriter change(order); change.write_u8(56); change.write_u8(0); change.write_u16(4); change.write_u32(base+2); change.write_u32(1U<<0U); change.write_u32(6);
    result=dispatch_request(state,context,finish(std::move(change),x11::CoreOpcode::ChangeGC,0));
    gw::test::require(result.output.size()==32&&result.output[1]==static_cast<std::uint8_t>(x11::CoreErrorCode::BadValue)&&state.resources().find_gc(base+2)->function==3,"atomic ChangeGC");

    x11::ByteWriter plane(order); plane.write_u8(56); plane.write_u8(0); plane.write_u16(4);
    plane.write_u32(base+2); plane.write_u32(1U<<1U); plane.write_u32(0x0000ff00U);
    result=dispatch_request(state,context,finish(std::move(plane),x11::CoreOpcode::ChangeGC,0));
    gw::test::require(result.output.empty(), "ChangeGC plane mask");
    x11::ByteWriter image(order); image.write_u8(72); image.write_u8(2); image.write_u16(7);
    image.write_u32(base+1); image.write_u32(base+2); image.write_u16(1); image.write_u16(1);
    image.write_u16(0); image.write_u16(0); image.write_u8(0); image.write_u8(24); image.write_u16(0);
    image.write_u8(0x66); image.write_u8(0x55); image.write_u8(0x44); image.write_u8(0x00);
    result=dispatch_request(state,context,finish(std::move(image),x11::CoreOpcode::PutImage,2));
    gw::test::require(result.output.empty()&&state.resources().find_pixmap(base+1)->storage->at(0,0)==0xff115533U,
                      "PutImage payload remains LSBFirst for either client order");

    x11::ByteWriter invalid_format(order); invalid_format.write_u8(72); invalid_format.write_u8(3); invalid_format.write_u16(6);
    invalid_format.write_u32(base+1); invalid_format.write_u32(base+2); invalid_format.write_u16(0); invalid_format.write_u16(0);
    invalid_format.write_u16(0); invalid_format.write_u16(0); invalid_format.write_u8(0); invalid_format.write_u8(24); invalid_format.write_u16(0);
    result=dispatch_request(state,context,finish(std::move(invalid_format),x11::CoreOpcode::PutImage,3));
    gw::test::require(result.output.size()==32&&result.output[1]==static_cast<std::uint8_t>(x11::CoreErrorCode::BadValue),
                      "invalid image format is BadValue");

    x11::ByteWriter second(order); second.write_u8(53); second.write_u8(24); second.write_u16(4);
    second.write_u32(base+3); second.write_u32(state.screen().root_window); second.write_u16(3); second.write_u16(2);
    result=dispatch_request(state,context,finish(std::move(second),x11::CoreOpcode::CreatePixmap,24));
    gw::test::require(result.output.empty(), "second pixmap");
    x11::ByteWriter copy(order); copy.write_u8(62); copy.write_u8(0); copy.write_u16(7);
    copy.write_u32(base+1); copy.write_u32(base+3); copy.write_u32(base+2);
    copy.write_u16(static_cast<std::uint16_t>(-1)); copy.write_u16(static_cast<std::uint16_t>(-1));
    copy.write_u16(0); copy.write_u16(0); copy.write_u16(3); copy.write_u16(2);
    result=dispatch_request(state,context,finish(std::move(copy),x11::CoreOpcode::CopyArea,0));
    gw::test::require(result.output.size()==64&&result.output[0]==13&&result.output[32]==13,
                      "CopyArea emits deterministic missing rectangles");
    const auto count_offset = 18U;
    gw::test::require((order==x11::ByteOrder::LittleEndian ? result.output[count_offset] : result.output[count_offset+1])==1,
                      "GraphicsExpose count decrements");

    WindowCreateSpec window; window.xid=base+4; window.parent=state.screen().root_window;
    window.width=4; window.height=3; window.window_class=WindowClass::InputOutput;
    window.attributes.background_source=BackgroundSource::Pixel;
    window.attributes.background_pixel=0x00010203U;
    gw::test::require(state.resources().create_window(1,base,mask,window)==CreateWindowStatus::Success,"clear target");
    x11::ByteWriter clear(order); clear.write_u8(61); clear.write_u8(1); clear.write_u16(4);
    clear.write_u32(base+4); clear.write_u16(static_cast<std::uint16_t>(-1)); clear.write_u16(1);
    clear.write_u16(0); clear.write_u16(0);
    result=dispatch_request(state,context,finish(std::move(clear),x11::CoreOpcode::ClearArea,1));
    gw::test::require(result.expose_intents.size()==1&&result.expose_intents[0].rectangle.width==4&&
                      result.expose_intents[0].rectangle.height==2&&result.drawable_damage.size()==1,
                      "ClearArea zero extents clip and expose intent");
  }
}
