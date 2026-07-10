#pragma once

#include "glasswyrmd/client_connection.hpp"
#include "glasswyrmd/options.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <sys/types.h>
#include <vector>

namespace glasswyrm::server {

class Server {
 public:
  explicit Server(Options options);
  ~Server();

  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  int run();

 private:
  bool open_listener();
  bool prepare_socket_path();
  bool remove_stale_socket();
  void accept_clients();
  void remove_closed_clients();
  void close_listener();
  void unlink_owned_socket();

  Options options_;
  std::string socket_path_;
  int listener_ = -1;
  dev_t socket_device_ = 0;
  ino_t socket_inode_ = 0;
  std::uint64_t next_client_identifier_ = 1;
  std::uint32_t next_resource_base_ = 0x00200000U;
  std::vector<std::unique_ptr<ClientConnection>> clients_;
};

}  // namespace glasswyrm::server
