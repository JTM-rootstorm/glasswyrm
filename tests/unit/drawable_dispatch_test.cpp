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
    gw::test::require(result.output.empty()&&state.resources().find_pixmap(base+1)->pixels()->at(1,1)==0xff112233U,"PolyFillRectangle");
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
    gw::test::require(result.output.empty()&&state.resources().find_pixmap(base+1)->pixels()->at(0,0)==0xff115533U,
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

    WindowCreateSpec clip_child; clip_child.xid=base+5; clip_child.parent=base+4; clip_child.x=1; clip_child.y=1;
    clip_child.width=2; clip_child.height=1; clip_child.window_class=WindowClass::InputOutput;
    gw::test::require(state.resources().create_window(1,base,mask,clip_child)==CreateWindowStatus::Success,
                      "ClipByChildren child");
    state.resources().find_window(base+4)->map_state=MapState::Viewable;
    state.resources().find_window(base+5)->map_requested=true;
    state.resources().find_window(base+5)->map_state=MapState::Viewable;
    state.resources().find_gc(base+2)->foreground=0x00abcdefU;
    state.resources().find_gc(base+2)->plane_mask=0x00ffffffU;
    x11::ByteWriter parent_fill(order); parent_fill.write_u8(70); parent_fill.write_u8(0); parent_fill.write_u16(5);
    parent_fill.write_u32(base+4); parent_fill.write_u32(base+2); parent_fill.write_u16(0); parent_fill.write_u16(0); parent_fill.write_u16(4); parent_fill.write_u16(3);
    result=dispatch_request(state,context,finish(std::move(parent_fill),x11::CoreOpcode::PolyFillRectangle,0));
    gw::test::require(state.resources().find_window(base+4)->storage->at(0,0)==0xffabcdefU&&
                      state.resources().find_window(base+4)->storage->at(1,1)==0xff010203U,
                      "ClipByChildren preserves parent pixels beneath mapped child");
    for (const auto& item : result.drawable_damage)
      gw::test::require(!glasswyrm::geometry::intersect(item.rectangle,{1,1,2,1}),
                        "ClipByChildren damage excludes child outer bounds");
    x11::ByteWriter unmap_child(order); unmap_child.write_u8(10); unmap_child.write_u8(0); unmap_child.write_u16(2); unmap_child.write_u32(base+5);
    result=dispatch_request(state,context,finish(std::move(unmap_child),x11::CoreOpcode::UnmapWindow,0));
    gw::test::require(result.expose_intents.size()==1&&result.expose_intents[0].window==base+4,
                      "unmapping child exposes preserved parent region");
    x11::ByteWriter revealed_fill(order); revealed_fill.write_u8(70); revealed_fill.write_u8(0); revealed_fill.write_u16(5);
    revealed_fill.write_u32(base+4); revealed_fill.write_u32(base+2); revealed_fill.write_u16(0); revealed_fill.write_u16(0); revealed_fill.write_u16(4); revealed_fill.write_u16(3);
    result=dispatch_request(state,context,finish(std::move(revealed_fill),x11::CoreOpcode::PolyFillRectangle,0));
    gw::test::require(state.resources().find_window(base+4)->storage->at(1,1)==0xffabcdefU,
                      "unmapped child no longer clips parent drawing");

    x11::ByteWriter line_gc(order); line_gc.write_u8(56); line_gc.write_u8(0); line_gc.write_u16(7);
    line_gc.write_u32(base+2); line_gc.write_u32((1U<<4U)|(1U<<5U)|(1U<<6U)|(1U<<7U));
    line_gc.write_u32(0); line_gc.write_u32(0); line_gc.write_u32(1); line_gc.write_u32(0);
    result=dispatch_request(state,context,finish(std::move(line_gc),x11::CoreOpcode::ChangeGC,0));
    gw::test::require(result.output.empty()&&state.resources().find_gc(base+2)->cap_style==1,
                      "GC line subset");
    x11::ByteWriter wide_gc(order); wide_gc.write_u8(56); wide_gc.write_u8(0); wide_gc.write_u16(4);
    wide_gc.write_u32(base+2); wide_gc.write_u32(1U<<4U); wide_gc.write_u32(1);
    result=dispatch_request(state,context,finish(std::move(wide_gc),x11::CoreOpcode::ChangeGC,0));
    gw::test::require(result.output.size()==32&&result.output[1]==static_cast<std::uint8_t>(x11::CoreErrorCode::BadImplementation)&&
                      state.resources().find_gc(base+2)->line_width==0,"unsupported line width is atomic");

    x11::ByteWriter line(order); line.write_u8(65); line.write_u8(1); line.write_u16(6);
    line.write_u32(base+1); line.write_u32(base+2);
    line.write_u16(0); line.write_u16(0); line.write_u16(1); line.write_u16(0); line.write_u16(0); line.write_u16(1);
    result=dispatch_request(state,context,finish(std::move(line),x11::CoreOpcode::PolyLine,1));
    gw::test::require(result.output.empty()&&state.resources().find_pixmap(base+1)->pixels()->at(1,1)!=0xff000000U,
                      "PolyLine CoordModePrevious");
    x11::ByteWriter segments(order); segments.write_u8(66); segments.write_u8(0); segments.write_u16(5);
    segments.write_u32(base+1); segments.write_u32(base+2);
    segments.write_u16(0); segments.write_u16(1); segments.write_u16(1); segments.write_u16(0);
    result=dispatch_request(state,context,finish(std::move(segments),x11::CoreOpcode::PolySegment,0));
    gw::test::require(result.output.empty(),"PolySegment");
    x11::ByteWriter polygon(order); polygon.write_u8(69); polygon.write_u8(0); polygon.write_u16(7);
    polygon.write_u32(base+1); polygon.write_u32(base+2); polygon.write_u8(2); polygon.write_u8(0); polygon.write_u16(0);
    polygon.write_u16(0); polygon.write_u16(0); polygon.write_u16(2); polygon.write_u16(0); polygon.write_u16(0); polygon.write_u16(2);
    result=dispatch_request(state,context,finish(std::move(polygon),x11::CoreOpcode::FillPoly,0));
    gw::test::require(result.output.empty(),"FillPoly convex origin");
    x11::ByteWriter ellipse(order); ellipse.write_u8(71); ellipse.write_u8(0); ellipse.write_u16(6);
    ellipse.write_u32(base+1); ellipse.write_u32(base+2); ellipse.write_u16(0); ellipse.write_u16(0);
    ellipse.write_u16(2); ellipse.write_u16(2); ellipse.write_u16(0); ellipse.write_u16(360*64);
    result=dispatch_request(state,context,finish(std::move(ellipse),x11::CoreOpcode::PolyFillArc,0));
    gw::test::require(result.output.empty(),"PolyFillArc full ellipse");
    x11::ByteWriter partial(order); partial.write_u8(71); partial.write_u8(0); partial.write_u16(6);
    partial.write_u32(base+1); partial.write_u32(base+2); partial.write_u16(0); partial.write_u16(0);
    partial.write_u16(2); partial.write_u16(2); partial.write_u16(0); partial.write_u16(90*64);
    result=dispatch_request(state,context,finish(std::move(partial),x11::CoreOpcode::PolyFillArc,0));
    gw::test::require(result.output.size()==32&&result.output[1]==static_cast<std::uint8_t>(x11::CoreErrorCode::BadImplementation),
                      "partial arc rejected before drawing");

    x11::ByteWriter bitmap_pixmap(order); bitmap_pixmap.write_u8(53); bitmap_pixmap.write_u8(1); bitmap_pixmap.write_u16(4);
    bitmap_pixmap.write_u32(base+7); bitmap_pixmap.write_u32(state.screen().root_window);
    bitmap_pixmap.write_u16(3); bitmap_pixmap.write_u16(1);
    result=dispatch_request(state,context,finish(std::move(bitmap_pixmap),x11::CoreOpcode::CreatePixmap,1));
    gw::test::require(result.output.empty(),"depth-one pixmap");
    x11::ByteWriter bitmap_gc(order); bitmap_gc.write_u8(55); bitmap_gc.write_u8(0); bitmap_gc.write_u16(7);
    bitmap_gc.write_u32(base+8); bitmap_gc.write_u32(base+7); bitmap_gc.write_u32((1U<<1U)|(1U<<2U)|(1U<<3U));
    bitmap_gc.write_u32(1); bitmap_gc.write_u32(1); bitmap_gc.write_u32(0);
    result=dispatch_request(state,context,finish(std::move(bitmap_gc),x11::CoreOpcode::CreateGC,0));
    gw::test::require(result.output.empty(),"depth-one GC");
    x11::ByteWriter bitmap_image(order); bitmap_image.write_u8(72); bitmap_image.write_u8(0); bitmap_image.write_u16(7);
    bitmap_image.write_u32(base+7); bitmap_image.write_u32(base+8); bitmap_image.write_u16(3); bitmap_image.write_u16(1);
    bitmap_image.write_u16(0); bitmap_image.write_u16(0); bitmap_image.write_u8(0); bitmap_image.write_u8(1); bitmap_image.write_u16(0);
    bitmap_image.write_u8(0b00000101); bitmap_image.write_padding(3);
    result=dispatch_request(state,context,finish(std::move(bitmap_image),x11::CoreOpcode::PutImage,0));
    const auto* bitmap=state.resources().find_pixmap(base+7)->bitmap();
    gw::test::require(result.output.empty()&&bitmap&&bitmap->at(0,0)==1&&bitmap->at(1,0)==0&&bitmap->at(2,0)==1,
                      "XYBitmap uses LSBFirst 32-bit rows in either client order");

    x11::ByteWriter xy_pixmap_image(order); xy_pixmap_image.write_u8(72); xy_pixmap_image.write_u8(1); xy_pixmap_image.write_u16(7);
    xy_pixmap_image.write_u32(base+7); xy_pixmap_image.write_u32(base+8); xy_pixmap_image.write_u16(3); xy_pixmap_image.write_u16(1);
    xy_pixmap_image.write_u16(0); xy_pixmap_image.write_u16(0); xy_pixmap_image.write_u8(0); xy_pixmap_image.write_u8(1); xy_pixmap_image.write_u16(0);
    xy_pixmap_image.write_u8(0b00000010); xy_pixmap_image.write_padding(3);
    result=dispatch_request(state,context,finish(std::move(xy_pixmap_image),x11::CoreOpcode::PutImage,1));
    gw::test::require(result.output.empty()&&bitmap->at(0,0)==0&&bitmap->at(1,0)==1&&bitmap->at(2,0)==0,
                      "one-plane depth-one XYPixmap uses the bitmap upload path");

    x11::ByteWriter stippled_gc(order); stippled_gc.write_u8(55); stippled_gc.write_u8(0); stippled_gc.write_u16(7);
    stippled_gc.write_u32(base+11); stippled_gc.write_u32(base+1);
    stippled_gc.write_u32((1U<<3U)|(1U<<8U)|(1U<<11U));
    stippled_gc.write_u32(0x00ffffffU); stippled_gc.write_u32(3); stippled_gc.write_u32(base+7);
    result=dispatch_request(state,context,finish(std::move(stippled_gc),x11::CoreOpcode::CreateGC,0));
    const auto* retained_stipple=state.resources().find_gc(base+11)->stipple.get();
    gw::test::require(result.output.empty()&&retained_stipple&&
                      state.resources().find_gc(base+11)->fill_style==3&&
                      state.resources().find_gc(base+11)->background==0x00ffffffU,
                      "Xaw opaque-stippled CreateGC mask");

    x11::ByteWriter bad_stipple(order); bad_stipple.write_u8(56); bad_stipple.write_u8(0); bad_stipple.write_u16(4);
    bad_stipple.write_u32(base+11); bad_stipple.write_u32(1U<<11U); bad_stipple.write_u32(base+6);
    result=dispatch_request(state,context,finish(std::move(bad_stipple),x11::CoreOpcode::ChangeGC,0));
    gw::test::require(result.output.size()==32&&result.output[1]==static_cast<std::uint8_t>(x11::CoreErrorCode::BadPixmap)&&
                      state.resources().find_gc(base+11)->stipple.get()==retained_stipple,
                      "invalid GCStipple is atomic BadPixmap");

    x11::ByteWriter bad_combination(order); bad_combination.write_u8(56); bad_combination.write_u8(0); bad_combination.write_u16(5);
    bad_combination.write_u32(base+2); bad_combination.write_u32((1U<<8U)|(1U<<11U));
    bad_combination.write_u32(3); bad_combination.write_u32(base+6);
    result=dispatch_request(state,context,finish(std::move(bad_combination),x11::CoreOpcode::ChangeGC,0));
    gw::test::require(result.output.size()==32&&result.output[1]==static_cast<std::uint8_t>(x11::CoreErrorCode::BadPixmap)&&
                      state.resources().find_gc(base+2)->fill_style==0&&!state.resources().find_gc(base+2)->stipple,
                      "combined fill-style and invalid stipple change is atomic");

    x11::ByteWriter wrong_depth(order); wrong_depth.write_u8(55); wrong_depth.write_u8(0); wrong_depth.write_u16(5);
    wrong_depth.write_u32(base+12); wrong_depth.write_u32(base+1); wrong_depth.write_u32(1U<<11U); wrong_depth.write_u32(base+1);
    result=dispatch_request(state,context,finish(std::move(wrong_depth),x11::CoreOpcode::CreateGC,0));
    gw::test::require(result.output.size()==32&&result.output[1]==static_cast<std::uint8_t>(x11::CoreErrorCode::BadMatch)&&
                      !state.resources().find_gc(base+12),"GCStipple requires depth-one pixmap");

    x11::ByteWriter tiled_style(order); tiled_style.write_u8(56); tiled_style.write_u8(0); tiled_style.write_u16(4);
    tiled_style.write_u32(base+11); tiled_style.write_u32(1U<<8U); tiled_style.write_u32(1);
    result=dispatch_request(state,context,finish(std::move(tiled_style),x11::CoreOpcode::ChangeGC,0));
    gw::test::require(result.output.size()==32&&result.output[1]==static_cast<std::uint8_t>(x11::CoreErrorCode::BadImplementation)&&
                      state.resources().find_gc(base+11)->fill_style==3,"unsupported valid fill style is atomic");

    x11::ByteWriter invalid_style(order); invalid_style.write_u8(56); invalid_style.write_u8(0); invalid_style.write_u16(4);
    invalid_style.write_u32(base+11); invalid_style.write_u32(1U<<8U); invalid_style.write_u32(4);
    result=dispatch_request(state,context,finish(std::move(invalid_style),x11::CoreOpcode::ChangeGC,0));
    gw::test::require(result.output.size()==32&&result.output[1]==static_cast<std::uint8_t>(x11::CoreErrorCode::BadValue)&&
                      state.resources().find_gc(base+11)->fill_style==3,"invalid fill style is atomic BadValue");

    x11::ByteWriter free_stipple(order); free_stipple.write_u8(54); free_stipple.write_u8(0); free_stipple.write_u16(2); free_stipple.write_u32(base+7);
    result=dispatch_request(state,context,finish(std::move(free_stipple),x11::CoreOpcode::FreePixmap,0));
    gw::test::require(result.output.empty()&&!state.resources().find_pixmap(base+7)&&
                      state.resources().find_gc(base+11)->stipple.get()==retained_stipple,
                      "GC retains freed stipple storage");

    x11::ByteWriter stippled_fill(order); stippled_fill.write_u8(70); stippled_fill.write_u8(0); stippled_fill.write_u16(5);
    stippled_fill.write_u32(base+1); stippled_fill.write_u32(base+11);
    stippled_fill.write_u16(static_cast<std::uint16_t>(-1)); stippled_fill.write_u16(0);
    stippled_fill.write_u16(5); stippled_fill.write_u16(1);
    result=dispatch_request(state,context,finish(std::move(stippled_fill),x11::CoreOpcode::PolyFillRectangle,0));
    gw::test::require(result.output.empty()&&state.resources().find_pixmap(base+1)->pixels()->at(0,0)==0xffffffffU&&
                      state.resources().find_pixmap(base+1)->pixels()->at(1,0)==0xff000000U,
                      "opaque stipple is drawable-anchored and clipped");

    x11::ByteWriter patterned_polygon(order); patterned_polygon.write_u8(69); patterned_polygon.write_u8(0); patterned_polygon.write_u16(7);
    patterned_polygon.write_u32(base+1); patterned_polygon.write_u32(base+11); patterned_polygon.write_u8(2); patterned_polygon.write_u8(0); patterned_polygon.write_u16(0);
    patterned_polygon.write_u16(0); patterned_polygon.write_u16(0); patterned_polygon.write_u16(2); patterned_polygon.write_u16(0); patterned_polygon.write_u16(0); patterned_polygon.write_u16(2);
    result=dispatch_request(state,context,finish(std::move(patterned_polygon),x11::CoreOpcode::FillPoly,0));
    gw::test::require(result.output.size()==32&&result.output[1]==static_cast<std::uint8_t>(x11::CoreErrorCode::BadImplementation),
                      "patterned FillPoly is explicitly unsupported");

    x11::ByteWriter patterned_arc(order); patterned_arc.write_u8(71); patterned_arc.write_u8(0); patterned_arc.write_u16(6);
    patterned_arc.write_u32(base+1); patterned_arc.write_u32(base+11); patterned_arc.write_u16(0); patterned_arc.write_u16(0);
    patterned_arc.write_u16(2); patterned_arc.write_u16(2); patterned_arc.write_u16(0); patterned_arc.write_u16(360*64);
    result=dispatch_request(state,context,finish(std::move(patterned_arc),x11::CoreOpcode::PolyFillArc,0));
    gw::test::require(result.output.size()==32&&result.output[1]==static_cast<std::uint8_t>(x11::CoreErrorCode::BadImplementation),
                      "patterned PolyFillArc is explicitly unsupported");

    WindowCreateSpec parent; parent.xid=base+9; parent.parent=state.screen().root_window;
    parent.width=8; parent.height=8; parent.window_class=WindowClass::InputOutput;
    gw::test::require(state.resources().create_window(1,base,mask,parent)==CreateWindowStatus::Success,
                      "nested parent");
    WindowCreateSpec child; child.xid=base+10; child.parent=base+9; child.x=2; child.y=3;
    child.border_width=1; child.width=2; child.height=2; child.window_class=WindowClass::InputOutput;
    gw::test::require(state.resources().create_window(1,base,mask,child)==CreateWindowStatus::Success,
                      "nested child");
    x11::ByteWriter child_fill(order); child_fill.write_u8(70); child_fill.write_u8(0); child_fill.write_u16(5);
    child_fill.write_u32(base+10); child_fill.write_u32(base+2);
    child_fill.write_u16(0); child_fill.write_u16(0); child_fill.write_u16(2); child_fill.write_u16(2);
    result=dispatch_request(state,context,finish(std::move(child_fill),x11::CoreOpcode::PolyFillRectangle,0));
    gw::test::require(result.output.empty()&&result.drawable_damage.size()==1&&
                      result.drawable_damage[0].window==base+9&&result.drawable_damage[0].rectangle.x==3&&
                      result.drawable_damage[0].rectangle.y==4,"child damage translates to top-level content");
  }
}
