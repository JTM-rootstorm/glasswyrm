#include "glasswyrmd/lifecycle_coordinator.hpp"

#include "tests/helpers/test_support.hpp"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace {
using namespace glasswyrm::server;
using gw::test::require;

LifecycleSnapshot snapshot(const std::uint32_t value) {
  LifecycleSnapshot result;
  result.focused_window = value;
  return result;
}

LifecycleOperation operation(const std::uint64_t token,
                             const std::uint64_t client,
                             const std::uint32_t value) {
  LifecycleOperation result;
  result.token = token;
  result.client_id = client;
  result.request_sequence = token;
  result.window = value;
  result.proposed = snapshot(value);
  return result;
}

struct Recorder {
  std::vector<std::uint32_t> policy;
  std::vector<std::uint32_t> compositor;
  std::vector<std::uint32_t> commits;
  std::vector<std::pair<std::uint64_t, bool>> completed;
  unsigned rollbacks_prepared{};
  unsigned compositor_retries_prepared{};
  bool fatal{};

  LifecycleCallbacks callbacks() {
    return {
        [&](const LifecycleSnapshot& value) {
          policy.push_back(value.focused_window);
          return true;
        },
        [&](const LifecycleSnapshot& value) {
          compositor.push_back(value.focused_window);
          return true;
        },
        [&](const LifecycleSnapshot& value) {
          commits.push_back(value.focused_window);
          return true;
        },
        [&](const std::uint64_t token, const bool success) {
          completed.emplace_back(token, success);
        },
        [&] { fatal = true; },
        {},
        [&] {
          ++rollbacks_prepared;
          return true;
        },
        [&] {
          ++compositor_retries_prepared;
          return true;
        }};
  }
};

void test_capacity_and_successful_fifo() {
  Recorder recorder;
  LifecycleCoordinator coordinator(snapshot(1), 2, recorder.callbacks());
  require(coordinator.enqueue(operation(10, 1, 10)) == EnqueueStatus::Queued &&
              coordinator.enqueue(operation(20, 2, 20)) ==
                  EnqueueStatus::Queued &&
              coordinator.enqueue(operation(30, 3, 30)) ==
                  EnqueueStatus::Full &&
              coordinator.pending_count() == 1,
          "capacity counts the active operation and preserves FIFO");
  require(coordinator.policy_accepted(snapshot(10)) &&
              coordinator.compositor_accepted() &&
              coordinator.committed().focused_window == 10 &&
              coordinator.active() && coordinator.active()->token == 20 &&
              recorder.policy == std::vector<std::uint32_t>({10, 20}),
          "successful commit promotes state and starts the next operation");
}

void test_priority_cleanup_runs_after_active_before_fifo() {
  Recorder recorder;
  LifecycleCoordinator coordinator(snapshot(1), 3, recorder.callbacks());
  auto cleanup = operation(30, 3, 30);
  cleanup.kind = LifecycleOperationKind::ClientCleanup;
  require(coordinator.enqueue(operation(10, 1, 10)) == EnqueueStatus::Queued &&
              coordinator.enqueue(operation(20, 2, 20)) ==
                  EnqueueStatus::Queued &&
              coordinator.enqueue_priority(std::move(cleanup)) ==
                  EnqueueStatus::Queued,
          "priority cleanup queues without interrupting active transaction");
  require(coordinator.policy_accepted(snapshot(10)) &&
              coordinator.compositor_accepted() && coordinator.active() &&
              coordinator.active()->token == 30 &&
              recorder.policy == std::vector<std::uint32_t>({10, 30}),
          "priority cleanup starts before ordinary pending FIFO work");
}

void test_replace_committed_requires_idle_boundary() {
  Recorder recorder;
  LifecycleCoordinator coordinator(snapshot(1), 2, recorder.callbacks());
  require(coordinator.can_replace_committed() &&
              coordinator.replace_committed(snapshot(2)) &&
              coordinator.committed().focused_window == 2,
          "output configuration may replace lifecycle truth while idle");
  require(coordinator.enqueue(operation(10, 1, 10)) == EnqueueStatus::Queued &&
              !coordinator.can_replace_committed() &&
              !coordinator.replace_committed(snapshot(3)) &&
              coordinator.committed().focused_window == 2,
          "failed lifecycle promotion preserves the exact old snapshot");
}

void test_compositor_rejection_rolls_back_both_peers() {
  Recorder recorder;
  LifecycleCoordinator coordinator(snapshot(1), 4, recorder.callbacks());
  require(coordinator.enqueue(operation(10, 1, 10)) == EnqueueStatus::Queued &&
              coordinator.policy_accepted(snapshot(11)) &&
              coordinator.compositor_rejected() &&
              coordinator.phase() == CoordinatorPhase::RollingBackPolicy &&
              recorder.policy == std::vector<std::uint32_t>({10, 1}),
          "compositor rejection starts committed policy rollback");
  require(coordinator.policy_accepted(snapshot(1)) &&
              coordinator.phase() == CoordinatorPhase::RollingBackCompositor &&
              recorder.compositor == std::vector<std::uint32_t>({11, 1}) &&
              coordinator.compositor_accepted() &&
              coordinator.committed().focused_window == 1 &&
              recorder.commits.empty() &&
              recorder.rollbacks_prepared == 1 &&
              recorder.completed.back() ==
                  std::pair<std::uint64_t, bool>{10, false},
          "rollback replays compositor and never commits rejected state");
}

void test_compositor_rollback_interruption_retries_only_compositor() {
  Recorder recorder;
  LifecycleCoordinator coordinator(snapshot(1), 4, recorder.callbacks());
  require(coordinator.enqueue(operation(10, 1, 10)) == EnqueueStatus::Queued &&
              coordinator.policy_accepted(snapshot(11)) &&
              coordinator.compositor_rejected() &&
              coordinator.policy_accepted(snapshot(1)) &&
              coordinator.phase() == CoordinatorPhase::RollingBackCompositor,
          "enter committed compositor rollback");
  const auto policy_before_retry = recorder.policy;
  require(coordinator.compositor_interrupted() &&
              coordinator.phase() == CoordinatorPhase::RollingBackCompositor &&
              recorder.policy == policy_before_retry &&
              recorder.compositor ==
                  std::vector<std::uint32_t>({11, 1, 1}) &&
              recorder.rollbacks_prepared == 1 &&
              recorder.compositor_retries_prepared == 1,
          "interrupted compositor rollback retries only committed compositor "
          "state");
  require(coordinator.compositor_accepted() && !recorder.fatal &&
              recorder.completed.back() ==
                  std::pair<std::uint64_t, bool>{10, false},
          "retried committed compositor rollback completes the rejection");
}

void test_forward_compositor_interruption_rolls_back_both_peers() {
  Recorder recorder;
  LifecycleCoordinator coordinator(snapshot(1), 4, recorder.callbacks());
  require(coordinator.enqueue(operation(10, 1, 10)) == EnqueueStatus::Queued &&
              coordinator.policy_accepted(snapshot(11)) &&
              coordinator.compositor_interrupted() &&
              coordinator.phase() == CoordinatorPhase::RollingBackPolicy &&
              recorder.policy == std::vector<std::uint32_t>({10, 1}) &&
              recorder.compositor == std::vector<std::uint32_t>({11}) &&
              recorder.rollbacks_prepared == 1 &&
              recorder.compositor_retries_prepared == 0,
          "interrupted forward compositor starts coordinated rollback");
  require(coordinator.policy_accepted(snapshot(1)) &&
              coordinator.compositor_accepted() && !recorder.fatal &&
              recorder.commits.empty() &&
              recorder.completed.back() ==
                  std::pair<std::uint64_t, bool>{10, false},
          "interrupted forward compositor rolls back without promotion");
}

void test_cancellation_before_policy_result() {
  Recorder recorder;
  LifecycleCoordinator coordinator(snapshot(1), 2, recorder.callbacks());
  require(coordinator.enqueue(operation(10, 7, 10)) == EnqueueStatus::Queued,
          "queue cancellable operation");
  require(coordinator.pending_policy_snapshot() != nullptr &&
              coordinator.pending_policy_snapshot()->focused_window == 10,
          "active proposal is the expected policy result");
  coordinator.cancel_client(7);
  require(coordinator.policy_accepted(snapshot(10)) &&
              coordinator.phase() == CoordinatorPhase::RollingBackPolicy &&
              recorder.compositor.empty() && recorder.policy.back() == 1 &&
              recorder.rollbacks_prepared == 1,
          "canceled accepted policy is never projected to compositor");
  require(coordinator.pending_policy_snapshot() != nullptr &&
              coordinator.pending_policy_snapshot()->focused_window == 1,
          "committed snapshot is the expected rollback policy result");
  require(coordinator.policy_accepted(snapshot(1)) &&
              coordinator.pending_policy_snapshot() == nullptr,
          "compositor rollback does not retain an expected policy result");
  require(coordinator.compositor_accepted() &&
              coordinator.pending_policy_snapshot() == nullptr &&
              recorder.completed.back() ==
                  std::pair<std::uint64_t, bool>{10, false},
          "canceled operation completes only after rollback");
}

void test_disconnect_replays_each_phase() {
  Recorder recorder;
  LifecycleCoordinator coordinator(snapshot(1), 2, recorder.callbacks());
  require(coordinator.enqueue(operation(10, 1, 10)) == EnqueueStatus::Queued,
          "queue replay operation");
  coordinator.peer_disconnected();
  require(coordinator.peer_synchronized() && recorder.policy.back() == 10,
          "AwaitingPolicy reconnect resends proposed policy");
  require(coordinator.policy_accepted(snapshot(11)), "accept replayed policy");
  coordinator.peer_disconnected();
  require(coordinator.peer_synchronized() && recorder.compositor.back() == 11,
          "AwaitingCompositor reconnect resends evaluated compositor");
  require(coordinator.compositor_rejected(), "enter policy rollback");
  coordinator.peer_disconnected();
  require(coordinator.peer_synchronized() && recorder.policy.back() == 1,
          "RollingBackPolicy reconnect resends committed policy");
  require(coordinator.policy_accepted(snapshot(1)),
          "advance to compositor rollback");
  coordinator.peer_disconnected();
  require(coordinator.peer_synchronized() && recorder.compositor.back() == 1,
          "RollingBackCompositor reconnect resends committed compositor");
  require(coordinator.compositor_accepted() && !recorder.fatal,
          "replayed rollback completes");
}

void test_commit_failure_is_fatal() {
  Recorder recorder;
  auto callbacks = recorder.callbacks();
  callbacks.commit = [](const LifecycleSnapshot&) { return false; };
  LifecycleCoordinator coordinator(snapshot(1), 1, std::move(callbacks));
  require(coordinator.enqueue(operation(10, 1, 10)) == EnqueueStatus::Queued &&
              coordinator.policy_accepted(snapshot(10)) &&
              !coordinator.compositor_accepted() &&
              coordinator.phase() == CoordinatorPhase::Fatal &&
              recorder.fatal && recorder.completed.empty() &&
              coordinator.committed().focused_window == 1,
          "commit failure is fatal without promotion or completion");
}

void test_rollback_preparation_failure_is_fatal() {
  Recorder recorder;
  auto callbacks = recorder.callbacks();
  callbacks.prepare_rollback = [] { return false; };
  LifecycleCoordinator coordinator(snapshot(1), 1, std::move(callbacks));
  require(coordinator.enqueue(operation(10, 1, 10)) == EnqueueStatus::Queued &&
              coordinator.policy_accepted(snapshot(10)) &&
              !coordinator.compositor_rejected() &&
              coordinator.phase() == CoordinatorPhase::Fatal &&
              recorder.fatal && recorder.policy.size() == 1,
          "rollback transport preparation failure is fatal before replay");
}

void test_queued_operation_rebases_on_latest_commit() {
  Recorder recorder;
  auto callbacks = recorder.callbacks();
  callbacks.rebase = [](const LifecycleSnapshot& committed,
                        const LifecycleOperation& intent)
      -> std::optional<LifecycleSnapshot> {
    return snapshot(committed.focused_window + intent.window);
  };
  LifecycleCoordinator coordinator(snapshot(1), 2, std::move(callbacks));
  require(coordinator.enqueue(operation(10, 1, 2)) == EnqueueStatus::Queued &&
              recorder.policy.back() == 3 &&
              coordinator.enqueue(operation(20, 2, 4)) == EnqueueStatus::Queued,
          "first intent rebases on initial committed state");
  require(coordinator.policy_accepted(snapshot(3)) &&
              coordinator.compositor_accepted() &&
              recorder.policy.back() == 7,
          "queued intent rebases after the preceding commit");
}

void test_paused_enqueue_waits_for_explicit_resume() {
  Recorder recorder;
  LifecycleCoordinator coordinator(snapshot(1), 3, recorder.callbacks());
  require(coordinator.enqueue_paused(operation(10, 1, 10)) ==
              EnqueueStatus::Queued &&
              coordinator.enqueue_priority_paused(operation(20, 2, 20)) ==
                  EnqueueStatus::Queued &&
              coordinator.phase() == CoordinatorPhase::Idle &&
              recorder.policy.empty(),
          "paused lifecycle and cleanup do not race an active content frame");
  require(coordinator.resume() && coordinator.active() &&
              coordinator.active()->token == 20 &&
              recorder.policy == std::vector<std::uint32_t>({20}),
          "explicit resume preserves paused cleanup priority");
  require(coordinator.policy_accepted(snapshot(20)) &&
              coordinator.compositor_accepted() && coordinator.active() &&
              coordinator.active()->token == 10,
          "normal paused lifecycle work follows priority cleanup");
}

void test_operation_rebase_preserves_unrelated_intent() {
  LifecycleSnapshot committed;
  committed.windows[10] = LifecycleWindow{};
  committed.windows[10].xid = 10;
  committed.windows[10].map_requested = true;
  committed.windows[10].map_serial = 7;
  committed.windows[10].requested_x = 40;
  committed.windows[10].geometry_serial = 8;

  LifecycleOperation configure;
  configure.kind = LifecycleOperationKind::Configure;
  configure.window = 10;
  configure.proposed = committed;
  configure.proposed.windows[10].map_requested = false;
  configure.proposed.windows[10].map_serial = 99;
  configure.proposed.windows[10].requested_x = 80;
  configure.proposed.windows[10].geometry_serial = 9;
  const auto configured = rebase_lifecycle_operation(committed, configure);
  require(configured && configured->windows.at(10).map_requested &&
              configured->windows.at(10).map_serial == 7 &&
              configured->windows.at(10).requested_x == 80 &&
              configured->windows.at(10).geometry_serial == 9,
          "Configure rebase preserves a preceding Map intent");

  LifecycleOperation unmap;
  unmap.kind = LifecycleOperationKind::Unmap;
  unmap.window = 10;
  unmap.proposed = *configured;
  unmap.proposed.windows[10].map_requested = false;
  unmap.proposed.windows[10].map_serial = 10;
  unmap.proposed.windows[10].requested_x = 999;
  unmap.proposed.windows[10].geometry_serial = 100;
  const auto unmapped = rebase_lifecycle_operation(*configured, unmap);
  require(unmapped && !unmapped->windows.at(10).map_requested &&
              unmapped->windows.at(10).map_serial == 10 &&
              unmapped->windows.at(10).requested_x == 80 &&
              unmapped->windows.at(10).geometry_serial == 9,
          "Map rebase preserves a preceding Configure intent");

  LifecycleOperation override_change;
  override_change.kind = LifecycleOperationKind::OverrideChange;
  override_change.window = 10;
  override_change.proposed = *unmapped;
  override_change.proposed.windows[10].override_redirect = true;
  override_change.proposed.windows[10].requested_x = 1234;
  const auto overridden =
      rebase_lifecycle_operation(*unmapped, override_change);
  require(overridden && overridden->windows.at(10).override_redirect &&
              overridden->windows.at(10).requested_x == 80,
          "OverrideChange rebase changes only override-redirect");
}

void test_create_destroy_rebase_latest_snapshot() {
  LifecycleSnapshot committed;
  committed.root_window = 1;
  committed.root_order = {10};
  committed.windows[10].xid = 10;
  LifecycleOperation create;
  create.kind = LifecycleOperationKind::Create;
  create.window = 20;
  create.proposed = committed;
  create.proposed.windows[20].xid = 20;
  const auto created = rebase_lifecycle_operation(committed, create);
  require(created && created->windows.contains(10) &&
              created->windows.contains(20) &&
              created->root_order == std::vector<std::uint32_t>({10, 20}),
          "Create rebase adds the staged candidate to latest committed state");
  LifecycleOperation destroy;
  destroy.kind = LifecycleOperationKind::Destroy;
  destroy.window = 10;
  destroy.proposed = *created;
  destroy.proposed.windows.erase(10);
  const auto destroyed = rebase_lifecycle_operation(*created, destroy);
  require(destroyed && !destroyed->windows.contains(10) &&
              destroyed->windows.contains(20) &&
              destroyed->root_order == std::vector<std::uint32_t>({20}),
          "Destroy rebase removes only its candidate from latest state");
}

}  // namespace

int main() {
  test_capacity_and_successful_fifo();
  test_priority_cleanup_runs_after_active_before_fifo();
  test_replace_committed_requires_idle_boundary();
  test_compositor_rejection_rolls_back_both_peers();
  test_compositor_rollback_interruption_retries_only_compositor();
  test_forward_compositor_interruption_rolls_back_both_peers();
  test_cancellation_before_policy_result();
  test_disconnect_replays_each_phase();
  test_commit_failure_is_fatal();
  test_rollback_preparation_failure_is_fatal();
  test_queued_operation_rebases_on_latest_commit();
  test_paused_enqueue_waits_for_explicit_resume();
  test_operation_rebase_preserves_unrelated_intent();
  test_create_destroy_rebase_latest_snapshot();
  return 0;
}
