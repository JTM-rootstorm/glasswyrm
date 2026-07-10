#ifndef GLASSWYRM_SRC_IPC_CONNECTION_INTERNAL_HPP
#define GLASSWYRM_SRC_IPC_CONNECTION_INTERNAL_HPP

#include "ipc/internal.hpp"

#include <span>

namespace gw::ipc {

gwipc_status queue_internal(gwipc_connection& connection, std::uint16_t type,
                            std::uint32_t flags, std::uint64_t reply_to,
                            std::span<const std::uint8_t> payload,
                            std::span<const int> fds = {});

}  // namespace gw::ipc

#endif
