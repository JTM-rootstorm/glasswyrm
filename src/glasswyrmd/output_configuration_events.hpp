#pragma once

#include "glasswyrmd/request_dispatcher.hpp"

#include <optional>
#include <vector>

namespace glasswyrm::server {

[[nodiscard]] std::optional<std::vector<ProtocolEventIntent>>
build_output_configuration_events(const ServerState& committed);

} // namespace glasswyrm::server
