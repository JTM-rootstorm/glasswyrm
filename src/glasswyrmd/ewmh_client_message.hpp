#pragma once

#include "glasswyrmd/request_dispatcher.hpp"

#include <optional>

namespace glasswyrm::server {

[[nodiscard]] std::optional<DispatchResult> handle_ewmh_client_message(
    ServerState& state, const DispatchContext& context,
    const gw::protocol::x11::FramedRequest& request,
    std::uint32_t destination,
    const gw::protocol::x11::ClientMessageEvent& event);

}  // namespace glasswyrm::server
