#pragma once

#include "glasswyrmd/lifecycle_types.hpp"
#include "glasswyrmd/output_configuration_coordinator.hpp"
#include "glasswyrmd/vrr_state_cache.hpp"

#include <glasswyrm/ipc.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace glasswyrm::server {

enum class OutputControlPollTag {
  ControlListener,
  ControlPeer,
};

struct OutputControlPollDescriptor {
  OutputControlPollTag tag{OutputControlPollTag::ControlListener};
  std::uint64_t peer_id{};
  int descriptor{-1};
  short events{};
  short revents{};
};

class OutputControlPeer final {
public:
  using WindowSnapshotProvider = std::function<LifecycleSnapshot()>;

  OutputControlPeer(std::string path, output::OutputLayout inventory,
                    WindowSnapshotProvider windows = {},
                    VrrStateCache* vrr = nullptr);
  ~OutputControlPeer();
  OutputControlPeer(const OutputControlPeer &) = delete;
  OutputControlPeer &operator=(const OutputControlPeer &) = delete;

  [[nodiscard]] bool start(std::string &error);
  [[nodiscard]] std::vector<OutputControlPollDescriptor>
  poll_descriptors() const;
  void service(std::span<const OutputControlPollDescriptor> descriptors);

  [[nodiscard]] std::size_t peer_count() const noexcept {
    return peers_.size();
  }
  [[nodiscard]] const std::string &path() const noexcept { return path_; }
  [[nodiscard]] OutputConfigurationCoordinator &coordinator() noexcept {
    return coordinator_;
  }
  [[nodiscard]] const OutputConfigurationCoordinator &coordinator()
      const noexcept {
    return coordinator_;
  }
  [[nodiscard]] bool transaction_owner_connected() const noexcept;
  [[nodiscard]] bool acknowledge_policy_rejected();
  [[nodiscard]] bool acknowledge_compositor_rejected(
      gw::ipc::wire::OutputConfigurationResult result);
  [[nodiscard]] bool acknowledge_rollback(bool succeeded);
  [[nodiscard]] bool acknowledge_committed();
  [[nodiscard]] bool acknowledge_internal_error();

private:
  struct ListenerDeleter {
    void operator()(gwipc_listener *value) const noexcept;
  };
  struct ConnectionDeleter {
    void operator()(gwipc_connection *value) const noexcept;
  };
  struct StagedSnapshot {
    std::uint64_t snapshot_id{};
    std::uint64_t generation{};
    std::uint32_t expected_count{};
    std::uint32_t actual_count{};
    std::vector<gw::ipc::wire::OutputUpsert> outputs;
    std::vector<gwipc_output_vrr_policy_upsert> vrr_policies;
    bool reading{};
    bool complete{};
  };
  struct Peer {
    std::uint64_t id{};
    std::unique_ptr<gwipc_connection, ConnectionDeleter> connection;
    StagedSnapshot staged;
  };
  struct PendingReply {
    std::uint64_t peer_id{};
    std::uint64_t sequence{};
    std::uint64_t request_id{};
  };

  void accept_peers();
  void service_peer(std::uint64_t peer_id, short revents);
  void drain(Peer &peer);
  [[nodiscard]] bool consume(Peer &peer, const gwipc_message *message);
  [[nodiscard]] bool consume_begin(Peer &peer, const gwipc_message *message);
  [[nodiscard]] bool consume_output(Peer &peer, const gwipc_message *message);
  [[nodiscard]] bool consume_vrr_policy(Peer& peer,
                                        const gwipc_message* message);
  [[nodiscard]] bool consume_end(Peer &peer, const gwipc_message *message);
  [[nodiscard]] bool consume_query(Peer &peer, const gwipc_message *message);
  [[nodiscard]] bool consume_commit(Peer &peer, const gwipc_message *message);
  [[nodiscard]] bool enqueue_acknowledgement(
      Peer &peer, std::uint64_t reply_to,
      const gw::ipc::wire::OutputConfigurationAcknowledged &acknowledgement);
  [[nodiscard]] bool finish_transaction(
      std::optional<gw::ipc::wire::OutputConfigurationAcknowledged> result);
  void disconnect(std::uint64_t peer_id) noexcept;
  [[nodiscard]] std::uint64_t take_snapshot_id() noexcept;

  std::string path_;
  WindowSnapshotProvider window_snapshot_provider_;
  VrrStateCache* vrr_{};
  std::unique_ptr<gwipc_listener, ListenerDeleter> listener_;
  std::map<std::uint64_t, Peer> peers_;
  OutputConfigurationCoordinator coordinator_;
  std::optional<PendingReply> pending_reply_;
  std::uint64_t next_peer_id_{1};
  std::uint64_t next_snapshot_id_{1};
};

} // namespace glasswyrm::server
