#ifndef GLASSWYRM_SRC_IPC_ENDPOINT_HPP
#define GLASSWYRM_SRC_IPC_ENDPOINT_HPP

#include "ipc/internal.hpp"

#include <sys/types.h>

#include <string>

namespace gw::ipc {

struct EndpointIdentity {
  dev_t device{};
  ino_t inode{};
};

gwipc_status prepare_endpoint_path(const std::string& path,
                                   int& system_errno) noexcept;
gwipc_status bind_endpoint(const std::string& path, int& out_fd,
                           EndpointIdentity& identity,
                           int& system_errno) noexcept;
void cleanup_endpoint(const std::string& path,
                      const EndpointIdentity& identity) noexcept;
gwipc_status connect_endpoint(const std::string& path, int& out_fd,
                              bool& in_progress,
                              int& system_errno) noexcept;
bool read_peer_credentials(int fd, gwipc_peer_info& peer,
                           int& system_errno) noexcept;

}  // namespace gw::ipc

#endif
