#include "glasswyrmd/lifecycle_coordinator.hpp"

namespace glasswyrm::server {

LifecycleCoordinator::LifecycleCoordinator(LifecycleSnapshot committed,
    const std::size_t maximum_pending, LifecycleCallbacks callbacks)
    : committed_(std::move(committed)), maximum_pending_(maximum_pending),
      callbacks_(std::move(callbacks)) {}

bool LifecycleCoordinator::send_policy(const LifecycleSnapshot& snapshot) {
  return callbacks_.send_policy && callbacks_.send_policy(snapshot);
}
bool LifecycleCoordinator::send_compositor(const LifecycleSnapshot& snapshot) {
  return callbacks_.send_compositor && callbacks_.send_compositor(snapshot);
}
void LifecycleCoordinator::fatal() {
  phase_ = CoordinatorPhase::Fatal;
  if (callbacks_.fatal) callbacks_.fatal();
}

EnqueueStatus LifecycleCoordinator::enqueue(LifecycleOperation operation) {
  if (phase_ == CoordinatorPhase::Fatal) return EnqueueStatus::Fatal;
  if (queue_.size() + (active_ ? 1U : 0U) >= maximum_pending_)
    return EnqueueStatus::Full;
  queue_.push_back(std::move(operation));
  if (phase_ == CoordinatorPhase::Idle && !start_next()) return EnqueueStatus::Fatal;
  return EnqueueStatus::Queued;
}

bool LifecycleCoordinator::start_next() {
  if (active_ || queue_.empty()) return true;
  active_ = std::move(queue_.front()); queue_.pop_front();
  phase_ = CoordinatorPhase::AwaitingPolicy;
  if (!send_policy(active_->proposed)) { fatal(); return false; }
  return true;
}

bool LifecycleCoordinator::policy_accepted(LifecycleSnapshot evaluated) {
  if (!active_) return false;
  if (phase_ == CoordinatorPhase::RollingBackPolicy) {
    phase_ = CoordinatorPhase::RollingBackCompositor;
    if (!send_compositor(committed_)) fatal();
    return phase_ != CoordinatorPhase::Fatal;
  }
  if (phase_ != CoordinatorPhase::AwaitingPolicy) return false;
  evaluated_ = std::move(evaluated);
  if (active_->canceled) {
    phase_ = CoordinatorPhase::RollingBackPolicy;
    if (!send_policy(committed_)) fatal();
    return phase_ != CoordinatorPhase::Fatal;
  }
  phase_ = CoordinatorPhase::AwaitingCompositor;
  if (!send_compositor(evaluated_)) fatal();
  return phase_ != CoordinatorPhase::Fatal;
}

bool LifecycleCoordinator::policy_rejected() {
  if (phase_ != CoordinatorPhase::AwaitingPolicy || !active_) return false;
  finish(false); return true;
}

bool LifecycleCoordinator::compositor_accepted() {
  if (!active_) return false;
  if (phase_ == CoordinatorPhase::AwaitingCompositor) {
    if (active_->canceled) {
      phase_ = CoordinatorPhase::RollingBackPolicy;
      if (!send_policy(committed_)) fatal();
      return phase_ != CoordinatorPhase::Fatal;
    }
    if (!callbacks_.commit || !callbacks_.commit(evaluated_)) { fatal(); return false; }
    committed_ = evaluated_; finish(true); return true;
  }
  if (phase_ == CoordinatorPhase::RollingBackCompositor) {
    finish(false); return true;
  }
  return false;
}

bool LifecycleCoordinator::compositor_rejected() {
  if (phase_ != CoordinatorPhase::AwaitingCompositor || !active_) return false;
  phase_ = CoordinatorPhase::RollingBackPolicy;
  if (!send_policy(committed_)) fatal();
  return phase_ != CoordinatorPhase::Fatal;
}

bool LifecycleCoordinator::peer_synchronized() {
  if (phase_ != CoordinatorPhase::WaitingForPeer) return false;
  phase_ = resume_phase_;
  const auto& snapshot = phase_ == CoordinatorPhase::AwaitingPolicy ||
                                  phase_ == CoordinatorPhase::RollingBackPolicy
                              ? (phase_ == CoordinatorPhase::AwaitingPolicy
                                     ? active_->proposed : committed_)
                              : (phase_ == CoordinatorPhase::AwaitingCompositor
                                     ? evaluated_ : committed_);
  const bool sent = phase_ == CoordinatorPhase::AwaitingPolicy ||
                            phase_ == CoordinatorPhase::RollingBackPolicy
                        ? send_policy(snapshot) : send_compositor(snapshot);
  if (!sent) fatal();
  return sent;
}

void LifecycleCoordinator::peer_disconnected() {
  if (phase_ == CoordinatorPhase::Fatal || phase_ == CoordinatorPhase::Idle ||
      phase_ == CoordinatorPhase::WaitingForPeer) return;
  resume_phase_ = phase_; phase_ = CoordinatorPhase::WaitingForPeer;
}

void LifecycleCoordinator::cancel_client(const std::uint64_t client) {
  for (auto& operation : queue_) if (operation.client_id == client) operation.canceled = true;
  if (active_ && active_->client_id == client) active_->canceled = true;
}

void LifecycleCoordinator::finish(const bool success) {
  const auto token = active_->token;
  if (callbacks_.complete) callbacks_.complete(token, success);
  active_.reset(); evaluated_ = {}; phase_ = CoordinatorPhase::Idle;
  while (!queue_.empty() && queue_.front().canceled) {
    const auto canceled = queue_.front().token; queue_.pop_front();
    if (callbacks_.complete) callbacks_.complete(canceled, false);
  }
  (void)start_next();
}

}  // namespace glasswyrm::server
