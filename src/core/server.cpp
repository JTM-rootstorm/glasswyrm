#include <glasswyrm/core/server.hpp>

#include <sstream>

namespace glasswyrm::core {

Server::Server(BuildConfig config) : config_(config) {}

const BuildConfig& Server::config() const noexcept {
  return config_;
}

ServerStatus Server::status() const noexcept {
  return status_;
}

std::string Server::describe() const {
  std::ostringstream stream;
  stream << project_name() << " " << config_.version << " server scaffold";
  stream << " [status=" << status_name(status_) << "]";
  return stream.str();
}

std::string status_name(ServerStatus status) {
  switch (status) {
    case ServerStatus::ScaffoldOnly:
      return "scaffold-only";
    case ServerStatus::Ready:
      return "ready";
    case ServerStatus::Error:
      return "error";
  }
  return "unknown";
}

}  // namespace glasswyrm::core
