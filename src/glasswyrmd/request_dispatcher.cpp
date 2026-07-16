#include "glasswyrmd/request_dispatcher.hpp"

#include "glasswyrmd/request_handlers/common.hpp"
#include "protocol/x11/core.hpp"

#include <new>

namespace glasswyrm::server {
namespace x11 = gw::protocol::x11;
using namespace request_handlers;

DispatchResult dispatch_request(ServerState& state,
                                const DispatchContext& context,
                                const x11::FramedRequest& request) {
  try {
    switch (static_cast<x11::CoreOpcode>(request.opcode)) {
      case x11::CoreOpcode::CreateWindow:
        return create_window(state, context, request);
      case x11::CoreOpcode::ChangeWindowAttributes:
        return change_window_attributes(state, context, request);
      case x11::CoreOpcode::MapWindow:
        return map_window(state, context, request, true);
      case x11::CoreOpcode::MapSubwindows:
        return map_subwindows(state, context, request, true);
      case x11::CoreOpcode::UnmapWindow:
        return map_window(state, context, request, false);
      case x11::CoreOpcode::UnmapSubwindows:
        return map_subwindows(state, context, request, false);
      case x11::CoreOpcode::ConfigureWindow:
        return configure_window(state, context, request);
      case x11::CoreOpcode::GetWindowAttributes:
        return get_window_attributes(state, context, request);
      case x11::CoreOpcode::DestroyWindow:
        return destroy_window(state, context, request);
      case x11::CoreOpcode::GetGeometry:
        return get_geometry(state, context, request);
      case x11::CoreOpcode::QueryTree:
        return query_tree(state, context, request);
      case x11::CoreOpcode::InternAtom:
        return intern_atom(state, context, request);
      case x11::CoreOpcode::GetAtomName:
        return get_atom_name(state, context, request);
      case x11::CoreOpcode::ChangeProperty:
        return change_property(state, context, request);
      case x11::CoreOpcode::DeleteProperty:
        return delete_property(state, context, request);
      case x11::CoreOpcode::GetProperty:
        return get_property(state, context, request);
      case x11::CoreOpcode::ListProperties:
        return list_properties(state, context, request);
      case x11::CoreOpcode::SetSelectionOwner:
        return set_selection_owner(state, context, request);
      case x11::CoreOpcode::GetSelectionOwner:
        return get_selection_owner(state, context, request);
      case x11::CoreOpcode::ConvertSelection:
        return convert_selection(state, context, request);
      case x11::CoreOpcode::SendEvent:
        return send_event(state, context, request);
      case x11::CoreOpcode::GrabPointer:
        return grab_pointer(state, context, request);
      case x11::CoreOpcode::UngrabPointer:
        return ungrab_pointer(state, context, request);
      case x11::CoreOpcode::GrabButton:
        return grab_button(state, context, request);
      case x11::CoreOpcode::ChangeActivePointerGrab:
        return change_active_pointer_grab(state, context, request);
      case x11::CoreOpcode::GrabKeyboard:
        return grab_keyboard(state, context, request);
      case x11::CoreOpcode::UngrabKeyboard:
        return ungrab_keyboard(state, context, request);
      case x11::CoreOpcode::AllowEvents:
        return allow_events(state, context, request);
      case x11::CoreOpcode::QueryPointer:
        return query_pointer(state, context, request);
      case x11::CoreOpcode::TranslateCoordinates:
        return translate_coordinates(state, context, request);
      case x11::CoreOpcode::QueryKeymap:
        return query_keymap(context, request);
      case x11::CoreOpcode::GetInputFocus:
        return get_input_focus(state, context, request);
      case x11::CoreOpcode::OpenFont:
        return open_font(state, context, request);
      case x11::CoreOpcode::CloseFont:
        return close_font(state, context, request);
      case x11::CoreOpcode::QueryFont:
        return query_font(state, context, request);
      case x11::CoreOpcode::QueryTextExtents:
        return query_text_extents(state, context, request);
      case x11::CoreOpcode::ListFonts:
        return list_fonts(context, request);
      case x11::CoreOpcode::AllocColor:
        return alloc_color(state, context, request);
      case x11::CoreOpcode::AllocNamedColor:
        return named_color(state, context, request, true);
      case x11::CoreOpcode::FreeColors:
        return free_colors(state, context, request);
      case x11::CoreOpcode::QueryColors:
        return query_colors(state, context, request);
      case x11::CoreOpcode::LookupColor:
        return named_color(state, context, request, false);
      case x11::CoreOpcode::CreateCursor:
        return create_cursor(state, context, request);
      case x11::CoreOpcode::CreateGlyphCursor:
        return create_glyph_cursor(state, context, request);
      case x11::CoreOpcode::FreeCursor:
        return free_cursor(state, context, request);
      case x11::CoreOpcode::RecolorCursor:
        return recolor_cursor(state, context, request);
      case x11::CoreOpcode::QueryBestSize:
        return query_best_size(state, context, request);
      case x11::CoreOpcode::QueryExtension:
        return query_extension(context, request);
      case x11::CoreOpcode::ListExtensions:
        return list_extensions(context, request);
      case x11::CoreOpcode::GetKeyboardMapping:
        return get_keyboard_mapping(context, request);
      case x11::CoreOpcode::ChangeKeyboardControl:
        return change_keyboard_control(state, context, request);
      case x11::CoreOpcode::GetKeyboardControl:
        return get_keyboard_control(state, context, request);
      case x11::CoreOpcode::Bell:
        return bell(context, request);
      case x11::CoreOpcode::GetPointerMapping:
        return get_pointer_mapping(context, request);
      case x11::CoreOpcode::GetModifierMapping:
        return get_modifier_mapping(context, request);
      case x11::CoreOpcode::CreatePixmap:
        return create_pixmap(state, context, request);
      case x11::CoreOpcode::FreePixmap:
        return free_pixmap(state, context, request);
      case x11::CoreOpcode::CreateGC:
        return create_gc(state, context, request);
      case x11::CoreOpcode::ChangeGC:
        return change_gc(state, context, request);
      case x11::CoreOpcode::FreeGC:
        return free_gc(state, context, request);
      case x11::CoreOpcode::ClearArea:
        return clear_area(state, context, request);
      case x11::CoreOpcode::CopyArea:
        return copy_area_request(state, context, request);
      case x11::CoreOpcode::PolyLine:
        return poly_line(state, context, request);
      case x11::CoreOpcode::PolySegment:
        return poly_segment(state, context, request);
      case x11::CoreOpcode::FillPoly:
        return fill_poly(state, context, request);
      case x11::CoreOpcode::PolyFillRectangle:
        return poly_fill_rectangle(state, context, request);
      case x11::CoreOpcode::PolyFillArc:
        return poly_fill_arc(state, context, request);
      case x11::CoreOpcode::PutImage:
        return put_image(state, context, request);
      case x11::CoreOpcode::PolyText8:
        return poly_text8(state, context, request);
      case x11::CoreOpcode::ImageText8:
        return image_text8(state, context, request);
      case x11::CoreOpcode::NoOperation:
        return {};
      default:
        break;
    }
  } catch (const std::bad_alloc&) {
    return error(context, request, x11::CoreErrorCode::BadAlloc);
  }
  return error(context, request, x11::CoreErrorCode::BadRequest);
}

}  // namespace glasswyrm::server
