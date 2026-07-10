#pragma once

#include <string>
#include <sys/types.h>

namespace gw::test {

class ServerProcess {
 public:
  explicit ServerProcess(std::string executable, bool wait_until_ready = true);
  ~ServerProcess();

  ServerProcess(const ServerProcess&) = delete;
  ServerProcess& operator=(const ServerProcess&) = delete;

  [[nodiscard]] const std::string& socket_path() const { return socket_path_; }
  [[nodiscard]] const std::string& socket_dir() const { return socket_dir_; }
  [[nodiscard]] pid_t pid() const { return pid_; }
  [[nodiscard]] const std::string& executable() const { return executable_; }
  int stop(int signal_number = 15);

 private:
  void wait_for_socket();

  std::string executable_;
  std::string socket_dir_;
  std::string socket_path_;
  std::string log_path_;
  pid_t pid_ = -1;
};

}  // namespace gw::test
