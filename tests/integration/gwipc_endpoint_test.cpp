#include <glasswyrm/ipc.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

[[noreturn]] void fail(const char* message) {
  std::fprintf(stderr, "gwipc endpoint test: %s\n", message);
  std::exit(1);
}

void require(bool condition, const char* message) {
  if (!condition) fail(message);
}

gwipc_listener_options options(const char* path) {
  gwipc_listener_options result{};
  result.struct_size = sizeof(result);
  result.path = path;
  result.local_role = GWIPC_ROLE_TEST_CONSUMER;
  result.accepted_peer_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_TEST_PRODUCER);
  return result;
}

void make_stale_socket(const std::string& path) {
  const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
  require(fd >= 0, "cannot create stale socket");
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  require(path.size() < sizeof(address.sun_path), "test path is too long");
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  require(::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
          "cannot bind stale socket");
  (void)::close(fd);
}

}  // namespace

int main() {
  char temporary[] = "/tmp/gwipc-endpoint-XXXXXX";
  require(::mkdtemp(temporary) != nullptr, "mkdtemp failed");
  require(::chmod(temporary, 0700) == 0, "cannot set private directory mode");
  const std::string directory = temporary;
  const std::string path = directory + "/gwipc.sock";

  auto listener_options = options(path.c_str());
  gwipc_listener* listener = nullptr;
  require(gwipc_listener_create(&listener_options, &listener) == GWIPC_STATUS_OK,
          "private listener creation failed");
  struct stat status {};
  require(::lstat(path.c_str(), &status) == 0 && S_ISSOCK(status.st_mode) &&
              (status.st_mode & 0777) == 0600,
          "listener did not create a mode-0600 socket");

  gwipc_listener* duplicate = nullptr;
  require(gwipc_listener_create(&listener_options, &duplicate) ==
              GWIPC_STATUS_INVALID_STATE &&
              duplicate == nullptr,
          "live listener was replaced");
  gwipc_listener_destroy(listener);
  require(::lstat(path.c_str(), &status) < 0 && errno == ENOENT,
          "owned listener socket was not cleaned up");

  FILE* file = std::fopen(path.c_str(), "w");
  require(file != nullptr, "cannot create regular-file obstacle");
  std::fclose(file);
  require(gwipc_listener_create(&listener_options, &listener) ==
              GWIPC_STATUS_INVALID_ARGUMENT &&
              ::lstat(path.c_str(), &status) == 0 && S_ISREG(status.st_mode),
          "regular-file obstacle was replaced");
  require(::unlink(path.c_str()) == 0, "cannot remove regular-file obstacle");

  const std::string target = directory + "/target";
  file = std::fopen(target.c_str(), "w");
  require(file != nullptr, "cannot create symlink target");
  std::fclose(file);
  require(::symlink(target.c_str(), path.c_str()) == 0, "cannot create symlink");
  require(gwipc_listener_create(&listener_options, &listener) ==
              GWIPC_STATUS_INVALID_ARGUMENT &&
              ::lstat(path.c_str(), &status) == 0 && S_ISLNK(status.st_mode),
          "symlink obstacle was replaced");
  require(::unlink(path.c_str()) == 0 && ::unlink(target.c_str()) == 0,
          "cannot remove symlink fixture");

  make_stale_socket(path);
  require(gwipc_listener_create(&listener_options, &listener) == GWIPC_STATUS_OK,
          "owned stale socket was not recovered");
  gwipc_listener_destroy(listener);

  std::string long_path = directory + "/" + std::string(200, 'x');
  auto long_options = options(long_path.c_str());
  require(gwipc_listener_create(&long_options, &listener) ==
              GWIPC_STATUS_INVALID_ARGUMENT,
          "overlong sockaddr_un path was accepted");

  const std::string parent_file = directory + "/parent-file";
  file = std::fopen(parent_file.c_str(), "w");
  require(file != nullptr, "cannot create parent-file fixture");
  std::fclose(file);
  const std::string child = parent_file + "/child.sock";
  auto child_options = options(child.c_str());
  require(gwipc_listener_create(&child_options, &listener) ==
              GWIPC_STATUS_INVALID_ARGUMENT,
          "non-directory parent was accepted");
  require(::unlink(parent_file.c_str()) == 0, "cannot remove parent fixture");
  require(::rmdir(directory.c_str()) == 0, "cannot remove temporary directory");
  return 0;
}
