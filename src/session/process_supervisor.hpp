#pragma once

#include <chrono>
#include <csignal>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

#include <sys/types.h>

namespace glasswyrm::session {

struct ChildSpec {
  std::string name;
  std::vector<std::string> argv;
  std::vector<std::string> environment;
  std::optional<std::string> readiness_socket;
  bool readiness_requires_socket = true;
  bool required = true;
  std::vector<std::string> additional_readiness_sockets;
  bool additional_readiness_requires_socket = true;
};

struct SupervisorOptions {
  std::chrono::milliseconds readiness_timeout{10000};
  std::chrono::milliseconds shutdown_timeout{1000};
  std::chrono::milliseconds poll_interval{10};
};

class ProcessSupervisor {
public:
  explicit ProcessSupervisor(SupervisorOptions options = {});
  ~ProcessSupervisor();

  ProcessSupervisor(const ProcessSupervisor &) = delete;
  ProcessSupervisor &operator=(const ProcessSupervisor &) = delete;

  [[nodiscard]] int run(const std::vector<ChildSpec> &specs,
                        std::ostream &error,
                        volatile std::sig_atomic_t *pending_signal = nullptr);

private:
  struct ReadinessPath {
    std::string path;
    bool requires_socket = true;
    bool existed_before_spawn = false;
    dev_t previous_device = 0;
    ino_t previous_inode = 0;
  };

  struct Child {
    ChildSpec spec;
    pid_t pid = -1;
    bool running = false;
    bool readiness_path_existed = false;
    dev_t readiness_device = 0;
    ino_t readiness_inode = 0;
    std::vector<ReadinessPath> additional_readiness;
  };

  [[nodiscard]] bool spawn(const ChildSpec &spec, std::ostream &error);
  [[nodiscard]] bool wait_until_ready(Child &child, std::ostream &error,
                                      volatile std::sig_atomic_t *signal);
  [[nodiscard]] std::optional<int> reap_nonblocking(std::size_t &child_index,
                                                    std::ostream &error);
  void shutdown_reverse(int signal, std::ostream &error) noexcept;
  void reap_all(std::ostream &error) noexcept;

  SupervisorOptions options_;
  std::vector<Child> children_;
};

} // namespace glasswyrm::session
