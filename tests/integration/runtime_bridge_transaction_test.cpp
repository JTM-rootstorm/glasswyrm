#include "glasswyrmd/runtime_bridge.hpp"
#include "tests/helpers/test_support.hpp"

#include <chrono>
#include <csignal>
#include <poll.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
using glasswyrm::server::RuntimeBridge;
using gw::test::require;

pid_t launch(const char* executable, const std::string& socket,
             const std::string& dump = {}) {
  const auto child = ::fork();
  require(child >= 0, "fork bridge peer");
  if (child == 0) {
    if (dump.empty())
      ::execl(executable, executable, "--ipc-socket", socket.c_str(), nullptr);
    else
      ::execl(executable, executable, "--ipc-socket", socket.c_str(),
              "--dump-dir", dump.c_str(), nullptr);
    _exit(127);
  }
  return child;
}

void stop(const pid_t child) {
  if (child <= 0) return;
  (void)::kill(child, SIGTERM);
  int status = 0;
  require(::waitpid(child, &status, 0) == child, "reap bridge peer");
}

bool service_once(RuntimeBridge& bridge, std::string& error) {
  pollfd descriptors[2]{{bridge.policy_fd(), bridge.policy_events(), 0},
                        {bridge.compositor_fd(), bridge.compositor_events(), 0}};
  require(::poll(descriptors, 2, 50) >= 0, "poll bridge transaction");
  return bridge.service(descriptors[0].revents, descriptors[1].revents,
                        RuntimeBridge::Clock::now(), error);
}

template <class Predicate>
void drive_until(RuntimeBridge& bridge, Predicate predicate,
                 const char* message) {
  std::string error;
  const auto deadline =
      RuntimeBridge::Clock::now() + std::chrono::seconds(5);
  while (!predicate()) {
    require(RuntimeBridge::Clock::now() < deadline, message);
    require(service_once(bridge, error), error.c_str());
  }
}

}  // namespace

int main(int argc, char** argv) {
  require(argc == 3, "expected gwm and gwcomp paths");
  char temporary[] = "/tmp/runtime-bridge-transaction-XXXXXX";
  require(::mkdtemp(temporary) != nullptr, "create bridge directory");
  const std::string root = temporary;
  const std::string policy_socket = root + "/gwm.sock";
  const std::string compositor_socket = root + "/gwcomp.sock";
  auto policy_process = launch(argv[1], policy_socket);
  const auto compositor_process =
      launch(argv[2], compositor_socket, root + "/dump");

  RuntimeBridge bridge(policy_socket, compositor_socket,
                       gw::protocol::x11::kScreenModel);
  bridge.start();
  drive_until(bridge, [&] { return bridge.ready(); },
              "bridge bootstrap timed out");

  std::string error;
  require(bridge.submit_policy({2, 2, {}}, error) &&
              !bridge.submit_policy({3, 3, {}}, error),
          "only one policy transaction may be staged");
  drive_until(bridge, [&] { return bridge.policy_result_ready(); },
              "policy result timed out");
  require(bridge.policy_result().generation == 2 &&
              bridge.prepare_rollback() &&
              bridge.submit_policy({3, 3, {}}, error),
          "policy-ready transaction can explicitly begin rollback");
  drive_until(bridge, [&] { return bridge.policy_result_ready(); },
              "rollback policy result timed out");
  require(bridge.submit_compositor({3, 3, {}, {}}, error),
          "accepted rollback policy advances to compositor replay");
  drive_until(bridge, [&] { return bridge.compositor_result_ready(); },
              "compositor result timed out");
  require(bridge.prepare_rollback() &&
              bridge.submit_policy({4, 4, {}}, error),
          "completed transaction can explicitly begin policy rollback");

  stop(policy_process);
  policy_process = launch(argv[1], policy_socket);
  drive_until(bridge, [&] { return bridge.policy_result_ready(); },
              "peer restart replays bootstrap and pending policy transaction");
  require(bridge.policy_result().generation == 4,
          "restarted policy peer completes the retained transaction");

  bridge.start();
  require(!bridge.policy_result_ready() &&
              !bridge.compositor_result_ready() &&
              !bridge.prepare_rollback() &&
              !bridge.submit_policy({5, 5, {}}, error),
          "start resets transaction stage before peers synchronize");
  stop(compositor_process);
  stop(policy_process);
  return 0;
}
