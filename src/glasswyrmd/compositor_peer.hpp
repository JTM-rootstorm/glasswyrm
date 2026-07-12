#pragma once

#include "glasswyrmd/peer_transport.hpp"
#include "protocol/x11/screen_model.hpp"

#include <string>
#include <vector>

namespace glasswyrm::server {

struct CompositorSnapshotSubmission {
  std::uint64_t commit_id{}, generation{};
  std::vector<gwipc_surface_upsert> surfaces;
  std::vector<gwipc_surface_policy_upsert> policies;
  struct Buffer {
    gwipc_buffer_attach attach{};
    int fd{-1};
  };
  struct Damage {
    std::uint64_t surface_id{};
    std::vector<gwipc_damage_rectangle> rectangles;
  };
  std::vector<Buffer> buffers;
  std::vector<Damage> damages;
};

struct CompositorContentSubmission {
  std::uint64_t commit_id{}, generation{};
  std::vector<CompositorSnapshotSubmission::Damage> damages;
};

struct CompositorBufferRelease {
  std::uint64_t buffer_id{};
  gwipc_buffer_release_reason reason{GWIPC_BUFFER_RELEASE_INVALID};
};

class CompositorPeer {
public:
  CompositorPeer(std::string path, gw::protocol::x11::ScreenModel screen,
                 bool software_content = false);
  [[nodiscard]] bool connect(std::string &error);
  [[nodiscard]] PeerProcessOutcome process(short revents, std::string &error);
  [[nodiscard]] int fd() const noexcept { return transport_.fd(); }
  [[nodiscard]] short wanted_events() const noexcept {
    return transport_.wanted_events();
  }
  [[nodiscard]] PeerBootstrapState state() const noexcept { return state_; }
  [[nodiscard]] bool submit(const CompositorSnapshotSubmission &submission,
                            std::string &error);
  [[nodiscard]] bool submit_content(
      const CompositorContentSubmission& submission, std::string& error);
  [[nodiscard]] std::vector<CompositorBufferRelease> take_releases();
  [[nodiscard]] const CompositorSnapshotSubmission &
  replay_input() const noexcept {
    return replay_input_;
  }
  void disconnect() noexcept;

private:
  [[nodiscard]] bool send_bootstrap(std::string &error);
  [[nodiscard]] PeerProcessOutcome drain(std::string &error);

  PeerTransport transport_;
  gw::protocol::x11::ScreenModel screen_;
  PeerBootstrapState state_{PeerBootstrapState::Disconnected};
  std::uint64_t commit_sequence_{};
  CompositorSnapshotSubmission pending_;
  CompositorSnapshotSubmission replay_input_;
  CompositorContentSubmission pending_content_;
  std::vector<CompositorBufferRelease> releases_;
  bool software_content_{};
  bool content_submission_{};
};

} // namespace glasswyrm::server
