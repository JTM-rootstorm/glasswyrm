#pragma once

#include "glasswyrmd/peer_transport.hpp"
#include "protocol/x11/screen_model.hpp"

#include <cstdint>
#include <string>

namespace glasswyrm::server {

class PolicyPeer {
public:
  PolicyPeer(std::string path, gw::protocol::x11::ScreenModel screen);
  [[nodiscard]] bool connect(std::string &error);
  [[nodiscard]] bool process(short revents, std::string &error);
  [[nodiscard]] int fd() const noexcept { return transport_.fd(); }
  [[nodiscard]] short wanted_events() const noexcept {
    return transport_.wanted_events();
  }
  [[nodiscard]] PeerBootstrapState state() const noexcept { return state_; }
  [[nodiscard]] std::uint64_t policy_hash() const noexcept {
    return policy_hash_;
  }
  void disconnect() noexcept;

private:
  [[nodiscard]] bool send_bootstrap(std::string &error);
  [[nodiscard]] bool drain(std::string &error);

  PeerTransport transport_;
  gw::protocol::x11::ScreenModel screen_;
  PeerBootstrapState state_{PeerBootstrapState::Disconnected};
  std::uint64_t commit_sequence_{};
  std::uint64_t policy_hash_{};
  bool reply_snapshot_active_{};
  bool reply_snapshot_complete_{};
};

} // namespace glasswyrm::server
