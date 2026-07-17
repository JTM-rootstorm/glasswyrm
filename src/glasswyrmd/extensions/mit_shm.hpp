#pragma once

#include "glasswyrmd/request_dispatcher.hpp"

namespace glasswyrm::server::extensions {

[[nodiscard]] DispatchResult dispatch_mit_shm(
    ServerState& state, const DispatchContext& context,
    const gw::protocol::x11::FramedRequest& request);

}  // namespace glasswyrm::server::extensions
