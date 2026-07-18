#pragma once

#include "output/model/layout.hpp"

#include <glasswyrm/ipc.h>

#include <cstdint>
#include <string>

namespace glasswyrm::compositor {

enum class OutputInventoryDisposition {
  NotHandled,
  Handled,
  RejectPeer,
  Fatal,
};

struct OutputInventoryServiceResult {
  OutputInventoryDisposition disposition{OutputInventoryDisposition::NotHandled};
  gwipc_status status{GWIPC_STATUS_OK};
  std::string reason;
};

class OutputInventoryService final {
 public:
  explicit OutputInventoryService(const output::OutputLayout& layout)
      : layout_(layout) {}

  [[nodiscard]] OutputInventoryServiceResult
  service(gwipc_connection& connection, gwipc_role peer_role,
          const gwipc_message& message);

 private:
  [[nodiscard]] std::uint64_t allocate_snapshot_id() noexcept;

  const output::OutputLayout& layout_;
  std::uint64_t next_snapshot_id_{1};
};

}  // namespace glasswyrm::compositor
