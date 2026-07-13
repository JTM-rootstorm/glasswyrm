#pragma once

#include "gwcomp/compositor.hpp"

#include <glasswyrm/ipc.h>

#include <cstdint>
#include <optional>

namespace glasswyrm::compositor {

struct ContractDispatchResult {
  bool accepted_frame{};
  bool stop_after_flush{};
  bool pending_frame{};
  bool fatal{};
  std::uint64_t reply_sequence{};
};

[[nodiscard]] ContractDispatchResult dispatch_contract_message(
    gwipc_connection* connection, gwipc_message* message, gwipc_role peer_role,
    gw::compositor::PeerProfile peer_profile,
    std::optional<std::uint64_t> maximum_frames,
    gw::compositor::Compositor& compositor);

[[nodiscard]] ContractDispatchResult complete_contract_presentation(
    gwipc_connection* connection, std::uint64_t reply_sequence,
    const gw::compositor::PresentationCompletion& completion,
    std::optional<std::uint64_t> maximum_frames,
    gw::compositor::Compositor& compositor);

} // namespace glasswyrm::compositor
