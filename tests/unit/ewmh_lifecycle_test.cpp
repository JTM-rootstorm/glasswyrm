#include "glasswyrmd/ewmh.hpp"
#include "glasswyrmd/lifecycle_coordinator.hpp"
#include "tests/unit/ewmh_test_support.hpp"

#include <utility>

namespace {

using namespace glasswyrm::server;
namespace x11 = gw::protocol::x11;
using ewmh_test::client_message;
using ewmh_test::create_window;
using ewmh_test::property_values;
using gw::test::require;

void test_active_root_synchronization(const x11::ByteOrder order) {
  ServerState state(kScreenModel, true);
  constexpr std::uint32_t first = 0x400010;
  constexpr std::uint32_t second = 0x400011;
  create_window(state, 7, first);
  create_window(state, 7, second);
  state.resources().find_window(first)->map_requested = true;
  state.resources().find_window(second)->map_requested = true;
  const std::array visible{
      AppliedPolicyWindow{first, 0, 0, 200, 120, 0, true, false},
      AppliedPolicyWindow{second, 0, 0, 200, 120, 1, true, false}};
  require(state.apply_policy(visible),
          "make active-window candidates visible");

  const auto active = state.atoms().find("_NET_ACTIVE_WINDOW").value();
  const auto root = state.screen().root_window;
  require(property_values(state, root, active) ==
              std::vector<std::uint32_t>({0}),
          "active-root property starts at None");
  DispatchContext context{1, 0x400000, 0x1fffff, 33, order, false};
  auto result = dispatch_request(
      state, context,
      client_message(order, root, first, active, {1, 100, 0, 0, 0}));
  require(result.output.empty() && state.focused_window() == first &&
              state.resources().find_window(first)->focused &&
              !state.resources().find_window(second)->focused &&
              property_values(state, root, active) ==
                  std::vector<std::uint32_t>({first}),
          "accepted active request synchronizes exact root and window focus");

  result = dispatch_request(
      state, context,
      client_message(order, root, second, active, {1, 101, 0, 0, 0}));
  require(result.output.empty() && state.focused_window() == second &&
              !state.resources().find_window(first)->focused &&
              state.resources().find_window(second)->focused &&
              property_values(state, root, active) ==
                  std::vector<std::uint32_t>({second}),
          "subsequent active request replaces root and window focus exactly");
}

void test_policy_rejection_and_rollback(const x11::ByteOrder order) {
  ServerState state(kScreenModel, true);
  constexpr std::uint32_t window = 0x400010;
  create_window(state, 7, window);
  const auto net_state = state.atoms().find("_NET_WM_STATE").value();
  const auto fullscreen =
      state.atoms().find("_NET_WM_STATE_FULLSCREEN").value();
  DispatchContext context{1, 0x400000, 0x1fffff, 34, order, true};
  const auto dispatched = dispatch_request(
      state, context,
      client_message(order, state.screen().root_window, window, net_state,
                     {1, fullscreen, 0, 1, 0}));
  require(dispatched.deferred_policy && dispatched.deferred_policy->property,
          "capture property-backed deferred EWMH transition");

  const auto committed = state.lifecycle_snapshot();
  auto proposed = committed;
  proposed.windows.at(window) = dispatched.deferred_policy->window;
  LifecycleOperation operation;
  operation.token = 90;
  operation.client_id = 7;
  operation.kind = LifecycleOperationKind::PolicyChange;
  operation.window = window;
  operation.proposed = proposed;

  std::vector<LifecycleSnapshot> policy;
  std::vector<LifecycleSnapshot> compositor;
  std::vector<std::pair<std::uint64_t, bool>> completed;
  bool committed_callback = false;
  LifecycleCallbacks callbacks{
      [&](const LifecycleSnapshot& snapshot) {
        policy.push_back(snapshot);
        return true;
      },
      [&](const LifecycleSnapshot& snapshot) {
        compositor.push_back(snapshot);
        return true;
      },
      [&](const LifecycleSnapshot&) {
        committed_callback = true;
        return true;
      },
      [&](const std::uint64_t token, const bool success) {
        completed.emplace_back(token, success);
      },
      [] {},
      {},
      [] { return true; },
      [] { return true; }};
  LifecycleCoordinator coordinator(committed, 2, callbacks);
  require(coordinator.enqueue(operation) == EnqueueStatus::Queued &&
              coordinator.policy_rejected() && policy.size() == 1 &&
              compositor.empty() && !committed_callback &&
              completed.back() ==
                  std::pair<std::uint64_t, bool>{90, false},
          "policy rejection completes without mutating or contacting compositor");

  policy.clear();
  compositor.clear();
  completed.clear();
  operation.token = 91;
  require(coordinator.enqueue(operation) == EnqueueStatus::Queued &&
              coordinator.policy_accepted(proposed) &&
              coordinator.compositor_rejected() &&
              coordinator.policy_accepted(committed) &&
              coordinator.compositor_accepted() && policy.size() == 2 &&
              compositor.size() == 2 &&
              policy.front().windows.at(window).fullscreen_requested &&
              !policy.back().windows.at(window).fullscreen_requested &&
              compositor.front().windows.at(window).fullscreen_requested &&
              !compositor.back().windows.at(window).fullscreen_requested &&
              !committed_callback && completed.back() ==
                  std::pair<std::uint64_t, bool>{91, false},
          "compositor rejection replays committed state through both peers");
  const auto* unchanged = state.resources().find_window(window);
  require(!unchanged->fullscreen_requested &&
              !unchanged->properties.contains(net_state),
          "rejected property lifecycle leaves server state unchanged");
}

}  // namespace

int main() {
  for (const auto order : {x11::ByteOrder::LittleEndian,
                           x11::ByteOrder::BigEndian}) {
    test_active_root_synchronization(order);
    test_policy_rejection_and_rollback(order);
  }
  return 0;
}
