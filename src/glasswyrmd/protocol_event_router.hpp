#pragma once

#include "glasswyrmd/client_connection.hpp"
#include "glasswyrmd/resource_table.hpp"

#include <cstddef>
#include <span>

namespace glasswyrm::server {

class ProtocolEventRouter {
 public:
  explicit ProtocolEventRouter(const ResourceTable& resources)
      : resources_(resources) {}

  [[nodiscard]] std::size_t route(
      const ProtocolEventIntent& intent,
      std::span<ClientConnection* const> clients) const;

 private:
  const ResourceTable& resources_;
};

}  // namespace glasswyrm::server
