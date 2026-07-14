#pragma once

#include "wm/transaction.hpp"

#include <glasswyrm/ipc.h>

#include <cstdint>

namespace glasswyrm::wm::runtime {

struct PeerState {
  Transaction transaction;
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
