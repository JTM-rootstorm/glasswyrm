#pragma once

#include "glasswyrmd/request_dispatcher.hpp"
#include "protocol/x11/core.hpp"

#include <cstddef>
#include <cstdint>

namespace glasswyrm::server::request_handlers {

namespace x11 = gw::protocol::x11;

[[nodiscard]] DispatchResult error(
    const DispatchContext& context, const x11::FramedRequest& request,
    x11::CoreErrorCode code, std::uint32_t bad_value = 0);
[[nodiscard]] bool exact_size(const x11::FramedRequest& request,
                              std::size_t size);
[[nodiscard]] std::size_t padded_size(std::size_t size);

[[nodiscard]] DispatchResult create_window(ServerState&, const DispatchContext&,
                                           const x11::FramedRequest&);
[[nodiscard]] DispatchResult destroy_window(ServerState&, const DispatchContext&,
                                            const x11::FramedRequest&);
[[nodiscard]] DispatchResult change_window_attributes(
    ServerState&, const DispatchContext&, const x11::FramedRequest&);
[[nodiscard]] DispatchResult get_window_attributes(
    ServerState&, const DispatchContext&, const x11::FramedRequest&);
[[nodiscard]] DispatchResult get_geometry(ServerState&, const DispatchContext&,
                                          const x11::FramedRequest&);
[[nodiscard]] DispatchResult map_window(ServerState&, const DispatchContext&,
                                        const x11::FramedRequest&, bool mapped);
[[nodiscard]] DispatchResult map_subwindows(
    ServerState&, const DispatchContext&, const x11::FramedRequest&, bool mapped);
[[nodiscard]] DispatchResult configure_window(
    ServerState&, const DispatchContext&, const x11::FramedRequest&);

[[nodiscard]] DispatchResult create_pixmap(ServerState&, const DispatchContext&,
                                           const x11::FramedRequest&);
[[nodiscard]] DispatchResult free_pixmap(ServerState&, const DispatchContext&,
                                         const x11::FramedRequest&);
[[nodiscard]] DispatchResult create_gc(ServerState&, const DispatchContext&,
                                       const x11::FramedRequest&);
[[nodiscard]] DispatchResult change_gc(ServerState&, const DispatchContext&,
                                       const x11::FramedRequest&);
[[nodiscard]] DispatchResult free_gc(ServerState&, const DispatchContext&,
                                     const x11::FramedRequest&);

[[nodiscard]] DispatchResult poly_line(ServerState&, const DispatchContext&,
                                       const x11::FramedRequest&);
[[nodiscard]] DispatchResult poly_segment(ServerState&, const DispatchContext&,
                                          const x11::FramedRequest&);
[[nodiscard]] DispatchResult fill_poly(ServerState&, const DispatchContext&,
                                       const x11::FramedRequest&);
[[nodiscard]] DispatchResult poly_fill_arc(ServerState&, const DispatchContext&,
                                           const x11::FramedRequest&);
[[nodiscard]] DispatchResult put_image(ServerState&, const DispatchContext&,
                                       const x11::FramedRequest&);
[[nodiscard]] DispatchResult poly_fill_rectangle(
    ServerState&, const DispatchContext&, const x11::FramedRequest&);
[[nodiscard]] DispatchResult copy_area_request(
    ServerState&, const DispatchContext&, const x11::FramedRequest&);
[[nodiscard]] DispatchResult clear_area(ServerState&, const DispatchContext&,
                                        const x11::FramedRequest&);

[[nodiscard]] bool valid_fontable(const ResourceTable&, std::uint32_t xid);
[[nodiscard]] DispatchResult open_font(ServerState&, const DispatchContext&,
                                       const x11::FramedRequest&);
[[nodiscard]] DispatchResult close_font(ServerState&, const DispatchContext&,
                                        const x11::FramedRequest&);
[[nodiscard]] DispatchResult query_font(ServerState&, const DispatchContext&,
                                        const x11::FramedRequest&);
[[nodiscard]] DispatchResult query_text_extents(
    ServerState&, const DispatchContext&, const x11::FramedRequest&);
[[nodiscard]] DispatchResult list_fonts(const DispatchContext&,
                                        const x11::FramedRequest&);
[[nodiscard]] DispatchResult image_text8(ServerState&, const DispatchContext&,
                                         const x11::FramedRequest&);
[[nodiscard]] DispatchResult poly_text8(ServerState&, const DispatchContext&,
                                        const x11::FramedRequest&);

[[nodiscard]] DispatchResult query_tree(ServerState&, const DispatchContext&,
                                        const x11::FramedRequest&);
[[nodiscard]] DispatchResult intern_atom(ServerState&, const DispatchContext&,
                                         const x11::FramedRequest&);
[[nodiscard]] DispatchResult get_atom_name(ServerState&, const DispatchContext&,
                                           const x11::FramedRequest&);
[[nodiscard]] DispatchResult change_property(ServerState&, const DispatchContext&,
                                             const x11::FramedRequest&);
[[nodiscard]] DispatchResult delete_property(ServerState&, const DispatchContext&,
                                             const x11::FramedRequest&);
[[nodiscard]] DispatchResult get_property(ServerState&, const DispatchContext&,
                                          const x11::FramedRequest&);
[[nodiscard]] DispatchResult list_properties(ServerState&, const DispatchContext&,
                                             const x11::FramedRequest&);
[[nodiscard]] DispatchResult set_selection_owner(
    ServerState&, const DispatchContext&, const x11::FramedRequest&);
[[nodiscard]] DispatchResult get_selection_owner(
    ServerState&, const DispatchContext&, const x11::FramedRequest&);
[[nodiscard]] DispatchResult convert_selection(
    ServerState&, const DispatchContext&, const x11::FramedRequest&);
[[nodiscard]] DispatchResult send_event(
    ServerState&, const DispatchContext&, const x11::FramedRequest&);

[[nodiscard]] DispatchResult get_input_focus(const ServerState&,
                                             const DispatchContext&,
                                             const x11::FramedRequest&);
[[nodiscard]] DispatchResult query_pointer(const ServerState&,
                                           const DispatchContext&,
                                           const x11::FramedRequest&);
[[nodiscard]] DispatchResult translate_coordinates(
    const ServerState&, const DispatchContext&, const x11::FramedRequest&);
[[nodiscard]] DispatchResult query_extension(const DispatchContext&,
                                             const x11::FramedRequest&);
[[nodiscard]] DispatchResult list_extensions(const DispatchContext&,
                                             const x11::FramedRequest&);
[[nodiscard]] DispatchResult get_keyboard_mapping(
    const DispatchContext&, const x11::FramedRequest&);
[[nodiscard]] DispatchResult get_pointer_mapping(
    const DispatchContext&, const x11::FramedRequest&);
[[nodiscard]] DispatchResult get_modifier_mapping(
    const DispatchContext&, const x11::FramedRequest&);

[[nodiscard]] DispatchResult alloc_color(const ServerState&,
                                         const DispatchContext&,
                                         const x11::FramedRequest&);
[[nodiscard]] DispatchResult named_color(const ServerState&,
                                         const DispatchContext&,
                                         const x11::FramedRequest&, bool allocate);
[[nodiscard]] DispatchResult free_colors(const ServerState&,
                                         const DispatchContext&,
                                         const x11::FramedRequest&);
[[nodiscard]] DispatchResult query_colors(const ServerState&,
                                          const DispatchContext&,
                                          const x11::FramedRequest&);
[[nodiscard]] DispatchResult create_cursor(ServerState&, const DispatchContext&,
                                           const x11::FramedRequest&);
[[nodiscard]] DispatchResult create_glyph_cursor(
    ServerState&, const DispatchContext&, const x11::FramedRequest&);
[[nodiscard]] DispatchResult free_cursor(ServerState&, const DispatchContext&,
                                         const x11::FramedRequest&);
[[nodiscard]] DispatchResult recolor_cursor(ServerState&, const DispatchContext&,
                                            const x11::FramedRequest&);
[[nodiscard]] DispatchResult query_best_size(
    const ServerState&, const DispatchContext&, const x11::FramedRequest&);

}  // namespace glasswyrm::server::request_handlers
