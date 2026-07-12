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
        {}};
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
              recorder.completed.back() ==
                  std::pair<std::uint64_t, bool>{10, false},
          "rollback replays compositor and never commits rejected state");
}

void test_cancellation_before_policy_result() {
  Recorder recorder;
  LifecycleCoordinator coordinator(snapshot(1), 2, recorder.callbacks());
  require(coordinator.enqueue(operation(10, 7, 10)) == EnqueueStatus::Queued,
          "queue cancellable operation");
  coordinator.cancel_client(7);
  require(coordinator.policy_accepted(snapshot(10)) &&
              coordinator.phase() == CoordinatorPhase::RollingBackPolicy &&
              recorder.compositor.empty() && recorder.policy.back() == 1,
          "canceled accepted policy is never projected to compositor");
  require(coordinator.policy_accepted(snapshot(1)) &&
              coordinator.compositor_accepted() &&
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

}  // namespace

int main() {
  test_capacity_and_successful_fifo();
  test_compositor_rejection_rolls_back_both_peers();
  test_cancellation_before_policy_result();
  test_disconnect_replays_each_phase();
  test_commit_failure_is_fatal();
  test_queued_operation_rebases_on_latest_commit();
  return 0;
}
