#pragma once

#include "glasswyrmd/client_connection.hpp"
#include "glasswyrmd/extension_registry.hpp"
#include "glasswyrmd/options.hpp"
#include "glasswyrmd/server_state.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <sys/types.h>
#include <vector>

namespace glasswyrm::server {

class ServerRuntime;

class Server {
 public:
  explicit Server(Options options);
  ~Server();

  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  int run();

 private:
  friend class ServerRuntime;

  bool open_listener();
  bool prepare_socket_path();
  bool remove_stale_socket();
  void accept_clients();
  [[nodiscard]] std::optional<std::uint32_t> allocate_resource_base() const;
  void remove_closed_clients();
  void close_listener();
  void unlink_owned_socket();

  Options options_;
  ExtensionRegistry extensions_;
  std::string socket_path_;
  int listener_ = -1;
  dev_t socket_device_ = 0;
  ino_t socket_inode_ = 0;
  std::uint64_t next_client_identifier_ = 1;
  ServerState state_;
  std::unique_ptr<CompatibilityTrace> trace_;
  std::vector<std::unique_ptr<ClientConnection>> clients_;
  ClientConnection::DeferredHandler deferred_lifecycle_handler_;
  ClientConnection::StructuralTransitionHandler structural_transition_handler_;
  ClientConnection::DrawableDamageHandler drawable_damage_handler_;
  ClientConnection::ExposeIntentHandler expose_intent_handler_;
  ClientConnection::ProtocolEventHandler protocol_event_handler_;
  ClientConnection::InputSnapshotProvider input_snapshot_provider_;
  std::function<void(std::uint64_t, std::uint32_t)> cancel_lifecycle_handler_;
  std::set<std::uint32_t> pending_resource_bases_;
};

}  // namespace glasswyrm::server
