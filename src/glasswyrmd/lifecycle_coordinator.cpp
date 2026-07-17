#include "glasswyrmd/lifecycle_coordinator.hpp"

namespace glasswyrm::server {

std::optional<LifecycleSnapshot> rebase_lifecycle_operation(
    const LifecycleSnapshot& committed, const LifecycleOperation& operation) {
  auto rebased = committed;
  const auto source = operation.proposed.windows.find(operation.window);
  const auto target = rebased.windows.find(operation.window);
  if (operation.kind == LifecycleOperationKind::Create) {
    if (source == operation.proposed.windows.end() ||
        target != rebased.windows.end()) return std::nullopt;
    rebased.windows.emplace(operation.window, source->second);
    rebased.root_order.push_back(operation.window);
    return rebased;
  }
  if (operation.kind == LifecycleOperationKind::Destroy) {
    if (target == rebased.windows.end()) return std::nullopt;
    rebased.windows.erase(target);
    std::erase(rebased.root_order, operation.window);
    if (rebased.focused_window == operation.window)
      rebased.focused_window = rebased.root_window;
    return rebased;
  }
  if (operation.kind == LifecycleOperationKind::ClientCleanup) {
    for (auto iterator = rebased.windows.begin();
         iterator != rebased.windows.end();) {
      if (!operation.proposed.windows.contains(iterator->first)) {
        std::erase(rebased.root_order, iterator->first);
        if (rebased.focused_window == iterator->first)
          rebased.focused_window = rebased.root_window;
        iterator = rebased.windows.erase(iterator);
      } else {
        ++iterator;
      }
    }
    return rebased;
  }
  if (source == operation.proposed.windows.end() ||
      target == rebased.windows.end()) return std::nullopt;
  switch (operation.kind) {
    case LifecycleOperationKind::Map:
    case LifecycleOperationKind::Unmap:
      target->second.map_requested = source->second.map_requested;
      target->second.map_serial = source->second.map_serial;
      break;
    case LifecycleOperationKind::Configure:
      target->second.requested_x = source->second.requested_x;
      target->second.requested_y = source->second.requested_y;
      target->second.requested_width = source->second.requested_width;
      target->second.requested_height = source->second.requested_height;
      target->second.requested_border_width =
          source->second.requested_border_width;
      target->second.geometry_serial = source->second.geometry_serial;
      target->second.stack_serial = source->second.stack_serial;
      target->second.stack_sibling = source->second.stack_sibling;
      target->second.stack_mode = source->second.stack_mode;
      break;
    case LifecycleOperationKind::OverrideChange:
      target->second.override_redirect = source->second.override_redirect;
      break;
    case LifecycleOperationKind::PolicyChange: {
      const auto applied_x = target->second.applied_x;
      const auto applied_y = target->second.applied_y;
      const auto applied_width = target->second.applied_width;
      const auto applied_height = target->second.applied_height;
      const auto stacking = target->second.stacking;
      const auto visible = target->second.policy_visible;
      const auto focused = target->second.focused;
      target->second = source->second;
      target->second.applied_x = applied_x;
      target->second.applied_y = applied_y;
      target->second.applied_width = applied_width;
      target->second.applied_height = applied_height;
      target->second.stacking = stacking;
      target->second.policy_visible = visible;
      target->second.focused = focused;
      break;
    }
    case LifecycleOperationKind::Focus:
      target->second.focus_serial = source->second.focus_serial;
      break;
    default: return std::nullopt;
  }
  return rebased;
}

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

EnqueueStatus LifecycleCoordinator::enqueue_paused(
    LifecycleOperation operation) {
  if (phase_ == CoordinatorPhase::Fatal) return EnqueueStatus::Fatal;
  if (queue_.size() + (active_ ? 1U : 0U) >= maximum_pending_)
    return EnqueueStatus::Full;
  queue_.push_back(std::move(operation));
  return EnqueueStatus::Queued;
}

bool LifecycleCoordinator::resume() {
  if (phase_ != CoordinatorPhase::Idle || active_) return false;
  return start_next();
}

EnqueueStatus LifecycleCoordinator::enqueue_priority(
    LifecycleOperation operation) {
  if (phase_ == CoordinatorPhase::Fatal) return EnqueueStatus::Fatal;
  if (queue_.size() + (active_ ? 1U : 0U) >= maximum_pending_)
    return EnqueueStatus::Full;
  queue_.push_front(std::move(operation));
  if (phase_ == CoordinatorPhase::Idle && !start_next())
    return EnqueueStatus::Fatal;
  return EnqueueStatus::Queued;
}

EnqueueStatus LifecycleCoordinator::enqueue_priority_paused(
    LifecycleOperation operation) {
  if (phase_ == CoordinatorPhase::Fatal) return EnqueueStatus::Fatal;
  if (queue_.size() + (active_ ? 1U : 0U) >= maximum_pending_)
    return EnqueueStatus::Full;
  queue_.push_front(std::move(operation));
  return EnqueueStatus::Queued;
}

bool LifecycleCoordinator::start_next() {
  if (active_ || queue_.empty()) return true;
  active_ = std::move(queue_.front()); queue_.pop_front();
  if (callbacks_.rebase) {
    auto rebased = callbacks_.rebase(committed_, *active_);
    if (!rebased) {
      finish(false);
      return phase_ != CoordinatorPhase::Fatal;
    }
    active_->proposed = std::move(*rebased);
  }
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
    if (callbacks_.prepare_rollback && !callbacks_.prepare_rollback()) {
      fatal();
      return false;
    }
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
      if (callbacks_.prepare_rollback && !callbacks_.prepare_rollback()) {
        fatal();
        return false;
      }
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
  if (callbacks_.prepare_rollback && !callbacks_.prepare_rollback()) {
    fatal();
    return false;
  }
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
