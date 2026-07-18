#pragma once

#include "glasswyrmd/request_dispatcher.hpp"

namespace glasswyrm::server::extensions {

[[nodiscard]] DispatchResult render_query_version(
    const DispatchContext&, const gw::protocol::x11::FramedRequest&);
[[nodiscard]] DispatchResult render_query_formats(
    const ServerState&, const DispatchContext&,
    const gw::protocol::x11::FramedRequest&);
[[nodiscard]] DispatchResult render_query_index_values(
    const DispatchContext&, const gw::protocol::x11::FramedRequest&);
[[nodiscard]] DispatchResult render_picture_request(
    ServerState&, const DispatchContext&,
    const gw::protocol::x11::FramedRequest&);
[[nodiscard]] DispatchResult render_raster_request(
    ServerState&, const DispatchContext&,
    const gw::protocol::x11::FramedRequest&);
[[nodiscard]] DispatchResult render_extension_error(
    const DispatchContext&, const gw::protocol::x11::FramedRequest&,
    std::uint8_t relative_error, std::uint32_t bad_value);

}  // namespace glasswyrm::server::extensions
