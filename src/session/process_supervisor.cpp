#include "session/process_supervisor.hpp"

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <ostream>
#include <spawn.h>
#include <thread>

extern char **environ;

namespace glasswyrm::session {
namespace {

int normalized_status(int status) noexcept {
  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  if (WIFSIGNALED(status))
    return 128 + WTERMSIG(status);
  return 1;
}

bool path_ready(const std::string &path, bool requires_socket) noexcept {
  struct stat status{};
  return ::lstat(path.c_str(), &status) == 0 &&
         (!requires_socket || S_ISSOCK(status.st_mode));
}

bool has_environment_key(const std::string &entry,
                         const std::string &replacement) {
  const auto separator = replacement.find('=');
  return separator != std::string::npos && entry.size() > separator &&
         entry[separator] == '=' &&
         entry.compare(0, separator, replacement, 0, separator) == 0;
}

void sleep_for(std::chrono::milliseconds duration) {
  std::this_thread::sleep_for(duration);
}

} // namespace

ProcessSupervisor::ProcessSupervisor(SupervisorOptions options)
    : options_(options) {}

ProcessSupervisor::~ProcessSupervisor() {
  bool active = false;
  for (const auto &child : children_)
    active |= child.running;
  if (!active)
    return;
  for (auto it = children_.rbegin(); it != children_.rend(); ++it)
    if (it->running)
      (void)::kill(it->pid, SIGKILL);
  for (auto &child : children_) {
    if (!child.running)
      continue;
    int status = 0;
    while (::waitpid(child.pid, &status, 0) < 0 && errno == EINTR) {
    }
    child.running = false;
  }
}

bool ProcessSupervisor::spawn(const ChildSpec &spec, std::ostream &error) {
  if (spec.argv.empty() || spec.argv.front().empty()) {
    error << "glasswyrm-session: empty argv for " << spec.name << '\n';
    return false;
  }
  std::vector<char *> argv;
  argv.reserve(spec.argv.size() + 1);
  for (const auto &argument : spec.argv)
    argv.push_back(const_cast<char *>(argument.c_str()));
  argv.push_back(nullptr);

  std::vector<std::string> environment_storage;
  for (char **entry = environ; entry && *entry; ++entry) {
    bool replaced = false;
    for (const auto &replacement : spec.environment)
      replaced |= has_environment_key(*entry, replacement);
    if (!replaced)
      environment_storage.emplace_back(*entry);
  }
  environment_storage.insert(environment_storage.end(),
                             spec.environment.begin(), spec.environment.end());
  std::vector<char *> environment;
  environment.reserve(environment_storage.size() + 1);
  for (auto &entry : environment_storage)
    environment.push_back(entry.data());
  environment.push_back(nullptr);

  posix_spawnattr_t attributes;
  int result = ::posix_spawnattr_init(&attributes);
  if (result != 0) {
    error << "glasswyrm-session: cannot initialize spawn attributes: "
          << std::strerror(result) << '\n';
    return false;
  }
  sigset_t empty;
  sigset_t defaults;
  ::sigemptyset(&empty);
  ::sigemptyset(&defaults);
  ::sigaddset(&defaults, SIGINT);
  ::sigaddset(&defaults, SIGTERM);
  ::sigaddset(&defaults, SIGHUP);
  ::sigaddset(&defaults, SIGQUIT);
  result = ::posix_spawnattr_setsigmask(&attributes, &empty);
  if (result == 0)
    result = ::posix_spawnattr_setsigdefault(&attributes, &defaults);
  if (result == 0)
    result = ::posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK |
                                                         POSIX_SPAWN_SETSIGDEF);

  pid_t pid = -1;
  if (result == 0)
    result = ::posix_spawnp(&pid, argv.front(), nullptr, &attributes,
                            argv.data(), environment.data());
  (void)::posix_spawnattr_destroy(&attributes);
  if (result != 0) {
    error << "glasswyrm-session: cannot start " << spec.name << ": "
          << std::strerror(result) << '\n';
    return false;
  }
  children_.push_back({spec, pid, true});
  return true;
}

std::optional<int> ProcessSupervisor::reap_nonblocking(std::size_t &child_index,
                                                       std::ostream &error) {
  for (std::size_t index = 0; index < children_.size(); ++index) {
    auto &child = children_[index];
    if (!child.running)
      continue;
    int status = 0;
    const pid_t result = ::waitpid(child.pid, &status, WNOHANG);
    if (result == 0)
      continue;
    if (result == child.pid) {
      child.running = false;
      child_index = index;
      return normalized_status(status);
    }
    if (result < 0 && errno != EINTR) {
      error << "glasswyrm-session: waitpid failed for " << child.spec.name
            << ": " << std::strerror(errno) << '\n';
      child.running = false;
      child_index = index;
      return 1;
    }
  }
  return std::nullopt;
}

bool ProcessSupervisor::wait_until_ready(
    Child &child, std::ostream &error,
    volatile std::sig_atomic_t *pending_signal) {
  if (!child.spec.readiness_socket)
    return true;
  const auto deadline =
      std::chrono::steady_clock::now() + options_.readiness_timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (pending_signal && *pending_signal != 0)
      return false;
    if (path_ready(*child.spec.readiness_socket,
                   child.spec.readiness_requires_socket))
      return true;
    std::size_t exited = 0;
    if (const auto status = reap_nonblocking(exited, error)) {
      error << "glasswyrm-session: " << children_[exited].spec.name
            << " exited before readiness with status " << *status << '\n';
      return false;
    }
    sleep_for(options_.poll_interval);
  }
  error << "glasswyrm-session: timed out waiting for " << child.spec.name
        << " readiness at " << *child.spec.readiness_socket << '\n';
  return false;
}

void ProcessSupervisor::shutdown_reverse(int signal,
                                         std::ostream &error) noexcept {
  for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
    if (!it->running)
      continue;
    if (::kill(it->pid, signal) < 0 && errno != ESRCH)
      error << "glasswyrm-session: cannot signal " << it->spec.name << ": "
            << std::strerror(errno) << '\n';
    const auto deadline =
        std::chrono::steady_clock::now() + options_.shutdown_timeout;
    int status = 0;
    while (std::chrono::steady_clock::now() < deadline) {
      const pid_t result = ::waitpid(it->pid, &status, WNOHANG);
      if (result == it->pid || (result < 0 && errno == ECHILD)) {
        it->running = false;
        break;
      }
      if (result < 0 && errno != EINTR)
        break;
      sleep_for(options_.poll_interval);
    }
    if (!it->running)
      continue;
    (void)::kill(it->pid, SIGKILL);
    while (::waitpid(it->pid, &status, 0) < 0 && errno == EINTR) {
    }
    it->running = false;
  }
}

void ProcessSupervisor::reap_all(std::ostream &error) noexcept {
  for (auto &child : children_) {
    if (!child.running)
      continue;
    int status = 0;
    while (::waitpid(child.pid, &status, 0) < 0) {
      if (errno == EINTR)
        continue;
      if (errno != ECHILD)
        error << "glasswyrm-session: waitpid failed for " << child.spec.name
              << ": " << std::strerror(errno) << '\n';
      break;
    }
    child.running = false;
  }
}

int ProcessSupervisor::run(const std::vector<ChildSpec> &specs,
                           std::ostream &error,
                           volatile std::sig_atomic_t *pending_signal) {
  children_.clear();
  if (specs.empty()) {
    error << "glasswyrm-session: no child processes configured\n";
    return 2;
  }
  for (const auto &spec : specs) {
    if (!spawn(spec, error) ||
        !wait_until_ready(children_.back(), error, pending_signal)) {
      const int signal =
          pending_signal && *pending_signal ? *pending_signal : SIGTERM;
      shutdown_reverse(signal, error);
      reap_all(error);
      return pending_signal && *pending_signal ? 128 + *pending_signal : 1;
    }
  }

  for (;;) {
    if (pending_signal && *pending_signal != 0) {
      const int signal = *pending_signal;
      shutdown_reverse(signal, error);
      reap_all(error);
      return 128 + signal;
    }
    std::size_t exited = 0;
    if (const auto status = reap_nonblocking(exited, error)) {
      const auto &child = children_[exited];
      if (child.spec.required) {
        error << "glasswyrm-session: required process " << child.spec.name
              << " exited with status " << *status << '\n';
        shutdown_reverse(SIGTERM, error);
        reap_all(error);
        return 1;
      }
      shutdown_reverse(SIGTERM, error);
      reap_all(error);
      return *status;
    }
    sleep_for(options_.poll_interval);
  }
}

} // namespace glasswyrm::session
