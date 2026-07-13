#include "glasswyrmd/server_runtime.hpp"

#include <cstdio>

namespace glasswyrm::server {

void ServerRuntime::shutdown(SignalRuntime& signals) {
  server_.clients_.clear();
  server_.deferred_lifecycle_handler_ = {};
  server_.cancel_lifecycle_handler_ = {};
  signals.close();
  server_.close_listener();
  server_.unlink_owned_socket();
  std::fprintf(stderr, "glasswyrmd: stopped\n");
}

}  // namespace glasswyrm::server
