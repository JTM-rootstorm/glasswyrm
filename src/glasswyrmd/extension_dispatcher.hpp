#pragma once

#include "glasswyrmd/request_dispatcher.hpp"

namespace glasswyrm::server {

[[nodiscard]] DispatchResult dispatch_extension_request(
    ServerState& state, const DispatchContext& context,
    const gw::protocol::x11::FramedRequest& request);

}  // namespace glasswyrm::server
