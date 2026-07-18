#pragma once

#include "glasswyrmd/request_dispatcher.hpp"

namespace glasswyrm::server::extensions {

[[nodiscard]] DispatchResult randr_query_version(
    const DispatchContext&, const gw::protocol::x11::FramedRequest&);
[[nodiscard]] DispatchResult randr_select_input(
    ServerState&, const DispatchContext&,
    const gw::protocol::x11::FramedRequest&);
[[nodiscard]] DispatchResult randr_get_screen_info(
    ServerState&, const DispatchContext&,
    const gw::protocol::x11::FramedRequest&);
[[nodiscard]] DispatchResult randr_get_screen_size_range(
    ServerState&, const DispatchContext&,
    const gw::protocol::x11::FramedRequest&);
[[nodiscard]] DispatchResult randr_get_screen_resources(
    ServerState&, const DispatchContext&,
    const gw::protocol::x11::FramedRequest&);
[[nodiscard]] DispatchResult randr_get_output_info(
    ServerState&, const DispatchContext&,
    const gw::protocol::x11::FramedRequest&);
[[nodiscard]] DispatchResult randr_list_output_properties(
    ServerState&, const DispatchContext&,
    const gw::protocol::x11::FramedRequest&);
[[nodiscard]] DispatchResult randr_query_output_property(
    ServerState&, const DispatchContext&,
    const gw::protocol::x11::FramedRequest&);
[[nodiscard]] DispatchResult randr_get_output_property(
    ServerState&, const DispatchContext&,
    const gw::protocol::x11::FramedRequest&);
[[nodiscard]] DispatchResult randr_get_crtc_info(
    ServerState&, const DispatchContext&,
    const gw::protocol::x11::FramedRequest&);
[[nodiscard]] DispatchResult randr_set_crtc_config(
    ServerState&, const DispatchContext&,
    const gw::protocol::x11::FramedRequest&);
[[nodiscard]] DispatchResult randr_get_crtc_gamma_size(
    ServerState&, const DispatchContext&,
    const gw::protocol::x11::FramedRequest&);
[[nodiscard]] DispatchResult randr_get_output_primary(
    ServerState&, const DispatchContext&,
    const gw::protocol::x11::FramedRequest&);

[[nodiscard]] DispatchResult randr_bad_output(
    const DispatchContext&, const gw::protocol::x11::FramedRequest&,
    std::uint32_t);
[[nodiscard]] DispatchResult randr_bad_crtc(
    const DispatchContext&, const gw::protocol::x11::FramedRequest&,
    std::uint32_t);
[[nodiscard]] DispatchResult randr_bad_mode(
    const DispatchContext&, const gw::protocol::x11::FramedRequest&,
    std::uint32_t);

}  // namespace glasswyrm::server::extensions
