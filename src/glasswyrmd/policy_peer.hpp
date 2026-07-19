#pragma once

#include "glasswyrmd/peer_transport.hpp"
#include "glasswyrmd/vrr_policy_projection.hpp"
#include "protocol/x11/screen_model.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace glasswyrm::server {

struct PolicySnapshotSubmission {
  PolicySnapshotSubmission() = default;
  PolicySnapshotSubmission(
      std::uint64_t commit, std::uint64_t producer_generation,
      std::vector<gwipc_policy_lifecycle_window_upsert> window_records,
      std::vector<gwipc_policy_output_upsert> output_records,
      std::vector<gwipc_policy_window_output_hint> hint_records,
      VrrPolicyProjection vrr_records = {})
      : commit_id(commit), generation(producer_generation),
        windows(std::move(window_records)), outputs(std::move(output_records)),
        output_hints(std::move(hint_records)), vrr(std::move(vrr_records)) {}

  std::uint64_t commit_id{}, generation{};
  std::vector<gwipc_policy_lifecycle_window_upsert> windows;
  std::vector<gwipc_policy_output_upsert> outputs;
  std::vector<gwipc_policy_window_output_hint> output_hints;
  VrrPolicyProjection vrr;
};

struct PolicySnapshotResult {
  std::uint64_t generation{}, hash{};
  std::vector<gwipc_policy_window_state> windows;
  std::optional<gwipc_policy_bindings_upsert> bindings;
  std::vector<gwipc_policy_output_vrr_state> vrr_outputs;
  std::vector<gwipc_policy_window_vrr_state> vrr_windows;
};

class PolicyPeer {
public:
  PolicyPeer(std::string path, gw::protocol::x11::ScreenModel screen,
             bool interactive_policy = true, bool output_model = false,
             bool vrr_profile = false);
  [[nodiscard]] bool connect(std::string &error);
  [[nodiscard]] PeerProcessOutcome process(short revents, std::string &error);
  [[nodiscard]] int fd() const noexcept { return transport_.fd(); }
  [[nodiscard]] short wanted_events() const noexcept {
    return transport_.wanted_events();
  }
  [[nodiscard]] PeerBootstrapState state() const noexcept { return state_; }
  [[nodiscard]] std::uint64_t policy_hash() const noexcept {
    return policy_hash_;
  }
  [[nodiscard]] bool submit(const PolicySnapshotSubmission &submission,
                            std::string &error);
  [[nodiscard]] bool ready_for_snapshot() const noexcept {
    return transport_.established() &&
           state_ == PeerBootstrapState::Connecting;
  }
  [[nodiscard]] const PolicySnapshotResult &result() const noexcept {
    return result_;
  }
  [[nodiscard]] const PolicySnapshotSubmission &replay_input() const noexcept {
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
  std::uint64_t policy_hash_{};
  bool reply_snapshot_active_{};
  bool reply_snapshot_complete_{};
  bool interactive_profile_{};
  bool output_model_profile_{};
  bool vrr_profile_{};
  bool replaying_{};
  PolicySnapshotSubmission pending_;
  PolicySnapshotSubmission replay_input_;
  PolicySnapshotResult result_;
};

} // namespace glasswyrm::server
