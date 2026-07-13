#pragma once

#include "glasswyrmd/server.hpp"

#ifdef GW_SERVER_HAS_IPC
#include "glasswyrmd/content_presenter.hpp"
#include "glasswyrmd/event_router.hpp"
#include "glasswyrmd/lifecycle_coordinator.hpp"
#include "glasswyrmd/runtime_bridge.hpp"
#include "glasswyrmd/synthetic_input_peer.hpp"
#include "input/input_state.hpp"
#endif

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <poll.h>
#include <vector>

namespace glasswyrm::server {

class SignalRuntime {
 public:
  SignalRuntime() = default;
  ~SignalRuntime();

  SignalRuntime(const SignalRuntime&) = delete;
  SignalRuntime& operator=(const SignalRuntime&) = delete;

  [[nodiscard]] bool start();
  void close() noexcept;
  [[nodiscard]] int read_descriptor() const noexcept { return descriptors_[0]; }
  [[nodiscard]] static bool stop_requested() noexcept;
  static void request_stop() noexcept;

 private:
  int descriptors_[2]{-1, -1};
};

class ServerRuntime {
 public:
  explicit ServerRuntime(Server& server) : server_(server) {}

  [[nodiscard]] int run();

 private:
  [[nodiscard]] bool initialize_trace();
  [[nodiscard]] int initialize_integrated(SignalRuntime& signals);
  [[nodiscard]] int event_loop(SignalRuntime& signals);
  void shutdown(SignalRuntime& signals);

#ifdef GW_SERVER_HAS_IPC
  struct PendingFocusInput {
    SyntheticInputRecord record;
    std::uint32_t old_focus{};
    std::size_t delivered{};
    bool provider_connected{true};
  };

  struct PendingMutation {
    std::optional<WindowCreateSpec> create;
    ClientId owner{};
    std::uint32_t resource_base{};
    std::uint32_t resource_mask{};
    std::uint64_t creation_serial{};
    std::optional<WindowDestroyPlan> destroy;
    std::optional<ClientCleanupPlan> cleanup;
  };

  void initialize_lifecycle();
  [[nodiscard]] bool send_policy(const LifecycleSnapshot& snapshot);
  [[nodiscard]] bool send_compositor(const LifecycleSnapshot& snapshot);
  [[nodiscard]] bool commit_lifecycle(const LifecycleSnapshot& snapshot);
  void complete_lifecycle(std::uint64_t token, bool success);
  [[nodiscard]] bool defer_lifecycle(ClientConnection& client,
                                     const DispatchResult& result);
  void cancel_client_lifecycle(std::uint64_t client,
                               std::uint32_t resource_base);
  [[nodiscard]] bool service_integrated(short policy_events,
                                        short compositor_events);
  void service_input(short listener_events, short connection_events);

  std::unique_ptr<RuntimeBridge> bridge_;
  std::unique_ptr<LifecycleCoordinator> lifecycle_;
  std::unique_ptr<ContentPresenter> content_presenter_;
  std::unique_ptr<SyntheticInputPeer> input_peer_;
  glasswyrm::input::InputState input_state_;
  std::uint64_t expected_input_id_{1};
  std::optional<PendingFocusInput> pending_focus_input_;
  std::uint64_t next_lifecycle_token_{1};
  // Runtime bootstrap owns commit/generation 1.
  std::uint64_t next_policy_commit_{2};
  std::uint64_t next_policy_generation_{2};
  std::uint64_t next_compositor_commit_{2};
  std::uint64_t next_compositor_generation_{2};
  std::uint64_t policy_commit_{0};
  std::uint64_t policy_generation_{0};
  std::optional<StructuralEventState> transition_before_;
  std::optional<InputTransitionState> input_transition_before_;
  bool content_replay_attempted_{false};
  std::map<std::uint64_t, PendingMutation> pending_mutations_;
#endif

  Server& server_;
};

}  // namespace glasswyrm::server
