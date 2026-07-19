#pragma once

#include "glasswyrmd/lifecycle_types.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <utility>

namespace glasswyrm::server {

enum class LifecycleOperationKind { Create, Map, Unmap, Configure, Destroy,
                                    OverrideChange, PolicyChange, ScaleChange,
                                    VrrChange, Focus, ClientCleanup };
enum class CoordinatorPhase { Idle, AwaitingPolicy, AwaitingCompositor,
                              RollingBackPolicy, RollingBackCompositor,
                              WaitingForPeer, Fatal };
enum class EnqueueStatus { Queued, Full, Fatal };

struct LifecycleOperation {
  std::uint64_t token{};
  std::uint64_t client_id{};
  std::uint64_t request_sequence{};
  LifecycleOperationKind kind{LifecycleOperationKind::Map};
  std::uint32_t window{};
  LifecycleSnapshot proposed;
  bool canceled{};
};

struct LifecycleCallbacks {
  std::function<bool(const LifecycleSnapshot&)> send_policy;
  std::function<bool(const LifecycleSnapshot&)> send_compositor;
  std::function<bool(const LifecycleSnapshot&)> commit;
  std::function<void(std::uint64_t, bool)> complete;
  std::function<void()> fatal;
  std::function<std::optional<LifecycleSnapshot>(
      const LifecycleSnapshot&, const LifecycleOperation&)> rebase;
  std::function<bool()> prepare_rollback;
};

[[nodiscard]] std::optional<LifecycleSnapshot> rebase_lifecycle_operation(
    const LifecycleSnapshot& committed, const LifecycleOperation& operation);

class LifecycleCoordinator {
 public:
  explicit LifecycleCoordinator(LifecycleSnapshot committed = {},
                                std::size_t maximum_pending = 1024,
                                LifecycleCallbacks callbacks = {});
  [[nodiscard]] EnqueueStatus enqueue(LifecycleOperation operation);
  [[nodiscard]] EnqueueStatus enqueue_paused(LifecycleOperation operation);
  [[nodiscard]] bool resume();
  [[nodiscard]] EnqueueStatus enqueue_priority(LifecycleOperation operation);
  [[nodiscard]] EnqueueStatus enqueue_priority_paused(
      LifecycleOperation operation);
  [[nodiscard]] bool policy_accepted(LifecycleSnapshot evaluated);
  [[nodiscard]] bool policy_rejected();
  [[nodiscard]] bool compositor_accepted();
  [[nodiscard]] bool compositor_rejected();
  void peer_disconnected();
  [[nodiscard]] bool peer_synchronized();
  void cancel_client(std::uint64_t client);

  [[nodiscard]] CoordinatorPhase phase() const noexcept { return phase_; }
  [[nodiscard]] std::size_t pending_count() const noexcept { return queue_.size(); }
  [[nodiscard]] const LifecycleOperation* active() const noexcept {
    return active_ ? &*active_ : nullptr;
  }
  [[nodiscard]] const LifecycleSnapshot& committed() const noexcept {
    return committed_;
  }
  [[nodiscard]] const LifecycleSnapshot* pending_policy_snapshot() const
      noexcept {
    if (!active_)
      return nullptr;
    if (phase_ == CoordinatorPhase::AwaitingPolicy)
      return &active_->proposed;
    if (phase_ == CoordinatorPhase::RollingBackPolicy)
      return &committed_;
    return nullptr;
  }
  [[nodiscard]] bool can_replace_committed() const noexcept {
    return phase_ == CoordinatorPhase::Idle && !active_;
  }
  [[nodiscard]] bool replace_committed(LifecycleSnapshot committed) {
    if (!can_replace_committed())
      return false;
    committed_ = std::move(committed);
    return true;
  }

 private:
  [[nodiscard]] bool start_next();
  [[nodiscard]] bool send_policy(const LifecycleSnapshot& snapshot);
  [[nodiscard]] bool send_compositor(const LifecycleSnapshot& snapshot);
  void finish(bool success);
  void fatal();

  LifecycleSnapshot committed_;
  LifecycleSnapshot evaluated_;
  std::deque<LifecycleOperation> queue_;
  std::optional<LifecycleOperation> active_;
  std::size_t maximum_pending_;
  LifecycleCallbacks callbacks_;
  CoordinatorPhase phase_{CoordinatorPhase::Idle};
  CoordinatorPhase resume_phase_{CoordinatorPhase::Idle};
};

}  // namespace glasswyrm::server
