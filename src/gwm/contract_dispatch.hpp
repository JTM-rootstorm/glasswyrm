#pragma once

#include "wm/transaction.hpp"
#include "wm/vrr_policy.hpp"

#include <glasswyrm/ipc.h>

#include <cstdint>
#include <optional>

namespace glasswyrm::wm::runtime {

struct PeerState {
  Transaction transaction;
  VrrInputs pending_vrr;
  VrrPolicyState committed_vrr;
  std::optional<VrrInputs> pre_snapshot_vrr;
  std::uint64_t snapshot_id{};
  std::uint64_t snapshot_generation{};
  std::uint64_t last_commit_id{};
  std::uint64_t last_generation{};

  void disconnect() noexcept;
};

[[nodiscard]] bool dispatch_control(PeerState& peer,
                                    const gwipc_message* message);
[[nodiscard]] bool dispatch_contract(PeerState& peer,
                                     gwipc_connection* connection,
                                     const gwipc_message* message,
                                     bool& accepted);

}  // namespace glasswyrm::wm::runtime
