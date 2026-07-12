#pragma once

#include <glasswyrm/ipc.hpp>

#include <cstdint>
#include <string>

namespace glasswyrm::server {

enum class PeerBootstrapState {
  Disconnected,
  Connecting,
  AwaitingReply,
  Synchronized,
  Failed,
};

class PeerTransport {
public:
  PeerTransport(std::string path, gwipc_role expected_role,
                gwipc_capabilities capabilities, std::string label);

  [[nodiscard]] bool connect(std::string &error);
  [[nodiscard]] bool process(short revents, std::string &error);
  void disconnect() noexcept;

  [[nodiscard]] int fd() const noexcept;
  [[nodiscard]] short wanted_events() const noexcept;
  [[nodiscard]] bool established() const noexcept;
  [[nodiscard]] gwipc_connection *connection() const noexcept;
  [[nodiscard]] glasswyrm::ipc::Connection &handle() noexcept {
    return connection_;
  }

private:
  std::string path_;
  gwipc_role expected_role_;
  gwipc_capabilities capabilities_;
  std::string label_;
  glasswyrm::ipc::Connection connection_;
};

} // namespace glasswyrm::server
