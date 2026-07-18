#pragma once

#include "output/model/layout.hpp"

#include <glasswyrm/ipc.h>

#include <cstdint>
#include <vector>

namespace glasswyrm::compositor {

struct OutputInventoryMessage {
  std::uint16_t type{};
  std::uint32_t flags{};
  std::uint64_t reply_to{};
  std::vector<std::uint8_t> payload;
};

struct OutputInventoryPublication {
  gwipc_status status{GWIPC_STATUS_OK};
  output::LayoutValidationResult layout_validation{};
  std::vector<OutputInventoryMessage> messages;

  [[nodiscard]] explicit operator bool() const noexcept {
    return status == GWIPC_STATUS_OK;
  }
};

// Builds one atomic reply to an already validated compositor OutputStateQuery.
// The caller owns snapshot identity and enqueues the returned messages in
// order.
[[nodiscard]] OutputInventoryPublication build_output_inventory_publication(
    const gwipc_output_state_query &query, std::uint64_t query_sequence,
    std::uint64_t snapshot_id, const output::OutputLayout &layout);

} // namespace glasswyrm::compositor
