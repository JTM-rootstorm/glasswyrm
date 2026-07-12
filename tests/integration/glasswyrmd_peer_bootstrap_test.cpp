#include "glasswyrmd/compositor_peer.hpp"
#include "glasswyrmd/policy_peer.hpp"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <poll.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
void require(bool value, const char *message) {
  if (!value) {
    std::fprintf(stderr, "peer bootstrap test: %s\n", message);
    std::exit(1);
  }
}
pid_t launch(const char *executable, const std::string &socket,
             const std::string &extra_name = {},
             const std::string &extra = {}) {
  const auto child = ::fork();
  require(child >= 0, "fork peer process");
  if (child == 0) {
    if (extra_name.empty())
      ::execl(executable, executable, "--ipc-socket", socket.c_str(), nullptr);
    else
      ::execl(executable, executable, "--ipc-socket", socket.c_str(),
              extra_name.c_str(), extra.c_str(), nullptr);
    _exit(127);
  }
  return child;
}
template <class Peer> void synchronize(Peer &peer) {
  std::string error;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!peer.connect(error)) {
    require(std::chrono::steady_clock::now() < deadline, error.c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  while (peer.state() != glasswyrm::server::PeerBootstrapState::Synchronized) {
    require(std::chrono::steady_clock::now() < deadline, "bootstrap timed out");
    pollfd descriptor{peer.fd(), peer.wanted_events(), 0};
    require(::poll(&descriptor, 1, 50) >= 0, "poll peer");
    require(peer.process(descriptor.revents, error), error.c_str());
  }
}
void stop(pid_t child) {
  (void)::kill(child, SIGTERM);
  int status = 0;
  require(::waitpid(child, &status, 0) == child, "reap peer process");
}
} // namespace

int main(int argc, char **argv) {
  require(argc == 3, "expected gwm and gwcomp paths");
  char temporary[] = "/tmp/glasswyrmd-peer-bootstrap-XXXXXX";
  require(::mkdtemp(temporary) != nullptr, "create temporary directory");
  const std::string root = temporary;
  const std::string wm_socket = root + "/gwm.sock";
  const std::string comp_socket = root + "/gwcomp.sock";
  const auto wm = launch(argv[1], wm_socket);
  glasswyrm::server::PolicyPeer policy(wm_socket,
                                       gw::protocol::x11::kScreenModel);
  synchronize(policy);
  require(policy.policy_hash() != 0, "policy bootstrap reports canonical hash");
  stop(wm);
  const auto compositor =
      launch(argv[2], comp_socket, "--dump-dir", root + "/dump");
  glasswyrm::server::CompositorPeer display(comp_socket,
                                            gw::protocol::x11::kScreenModel);
  synchronize(display);
  stop(compositor);
  return 0;
}
