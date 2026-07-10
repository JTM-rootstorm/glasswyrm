#include "integration/server_fixture.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace gw::test {
namespace {

std::string make_temp_directory() {
  std::string pattern = "/tmp/glasswyrmd-test-XXXXXX";
  if (::mkdtemp(pattern.data()) == nullptr) {
    throw std::runtime_error(std::strerror(errno));
  }
  return pattern;
}

}  // namespace

ServerProcess::ServerProcess(std::string executable, bool wait_until_ready)
    : executable_(std::move(executable)), socket_dir_(make_temp_directory()) {
  socket_path_ = socket_dir_ + "/X99";
  log_path_ = socket_dir_ + "/server.log";
  pid_ = ::fork();
  if (pid_ < 0) {
    throw std::runtime_error(std::strerror(errno));
  }
  if (pid_ == 0) {
    const int log = ::open(log_path_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (log >= 0) {
      (void)::dup2(log, STDOUT_FILENO);
      (void)::dup2(log, STDERR_FILENO);
      ::close(log);
    }
    ::execl(executable_.c_str(), executable_.c_str(), "--display", "99",
            "--socket-dir", socket_dir_.c_str(), nullptr);
    _exit(127);
  }
  if (wait_until_ready) {
    wait_for_socket();
  }
}

void ServerProcess::wait_for_socket() {
  for (int attempt = 0; attempt < 200; ++attempt) {
    struct stat status {};
    if (::lstat(socket_path_.c_str(), &status) == 0 && S_ISSOCK(status.st_mode)) {
      return;
    }
    int child_status = 0;
    const pid_t result = ::waitpid(pid_, &child_status, WNOHANG);
    if (result == pid_) {
      pid_ = -1;
      std::ifstream log(log_path_);
      throw std::runtime_error("server exited before readiness: " +
                               std::string(std::istreambuf_iterator<char>(log),
                                           std::istreambuf_iterator<char>()));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  throw std::runtime_error("server socket readiness timeout");
}

int ServerProcess::stop(int signal_number) {
  if (pid_ < 0) {
    return -1;
  }
  (void)::kill(pid_, signal_number);
  int status = 0;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const pid_t result = ::waitpid(pid_, &status, WNOHANG);
    if (result == pid_) {
      pid_ = -1;
      return status;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  (void)::kill(pid_, SIGKILL);
  (void)::waitpid(pid_, &status, 0);
  pid_ = -1;
  throw std::runtime_error("server failed to stop within timeout");
}

ServerProcess::~ServerProcess() {
  if (pid_ >= 0) {
    try {
      (void)stop();
    } catch (...) {
    }
  }
  std::error_code ignored;
  std::filesystem::remove_all(socket_dir_, ignored);
}

}  // namespace gw::test
