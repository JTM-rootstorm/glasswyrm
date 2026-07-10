#include "helpers/test_support.hpp"
#include "helpers/x11_fake_client.hpp"
#include "integration/server_fixture.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <exception>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

int main(int argc, char** argv) {
  using gw::protocol::x11::ByteOrder;
  gw::test::require(argc == 2, "daemon path argument is required");
  gw::test::ServerProcess server(argv[1]);

  const pid_t duplicate = ::fork();
  gw::test::require(duplicate >= 0, "fork duplicate daemon");
  if (duplicate == 0) {
    ::execl(argv[1], argv[1], "--display", "99", "--socket-dir",
            server.socket_dir().c_str(), nullptr);
    _exit(127);
  }
  int duplicate_status = 0;
  gw::test::require(::waitpid(duplicate, &duplicate_status, 0) == duplicate,
                    "reap duplicate daemon");
  gw::test::require(WIFEXITED(duplicate_status) &&
                        WEXITSTATUS(duplicate_status) != 0,
                    "duplicate daemon rejects active socket");

  for (int iteration = 0; iteration < 128; ++iteration) {
    gw::test::X11FakeClient client(server.socket_path());
    if ((iteration & 1) == 0) {
      client.send_all(gw::test::make_setup_request(ByteOrder::LittleEndian));
      gw::test::require(client.receive_setup_reply(ByteOrder::LittleEndian)[0] ==
                            1,
                        "repeated setup succeeds");
    }
  }
  gw::test::X11FakeClient final_client(server.socket_path());
  final_client.send_all(gw::test::make_setup_request(ByteOrder::LittleEndian));
  gw::test::require(final_client.receive_setup_reply(ByteOrder::LittleEndian)[0] ==
                        1,
                    "daemon remains healthy after connection loop");

  const std::string socket_path = server.socket_path();
  const int status = server.stop(SIGTERM);
  gw::test::require(WIFEXITED(status) && WEXITSTATUS(status) == 0,
                    "SIGTERM exits successfully");
  struct stat ignored {};
  gw::test::require(::lstat(socket_path.c_str(), &ignored) != 0 && errno == ENOENT,
                    "SIGTERM removes owned socket");

  const int stale = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  gw::test::require(stale >= 0, "create stale socket");
  sockaddr_un address {};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
  gw::test::require(
      ::bind(stale, reinterpret_cast<sockaddr*>(&address),
             static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) +
                                    socket_path.size() + 1)) == 0,
      "bind stale socket");
  ::close(stale);

  const pid_t replacement = ::fork();
  gw::test::require(replacement >= 0, "fork replacement daemon");
  if (replacement == 0) {
    ::execl(argv[1], argv[1], "--display", "99", "--socket-dir",
            server.socket_dir().c_str(), nullptr);
    _exit(127);
  }
  bool replacement_ready = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (::lstat(socket_path.c_str(), &ignored) == 0 && S_ISSOCK(ignored.st_mode)) {
      try {
        gw::test::X11FakeClient probe(socket_path);
        probe.send_all(gw::test::make_setup_request(ByteOrder::LittleEndian));
        replacement_ready =
            probe.receive_setup_reply(ByteOrder::LittleEndian)[0] == 1;
        if (replacement_ready) {
          break;
        }
      } catch (const std::exception&) {
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  gw::test::require(replacement_ready, "daemon replaces demonstrably stale socket");
  (void)::kill(replacement, SIGTERM);
  int replacement_status = 0;
  gw::test::require(::waitpid(replacement, &replacement_status, 0) == replacement,
                    "reap replacement daemon");
  gw::test::require(WIFEXITED(replacement_status) &&
                        WEXITSTATUS(replacement_status) == 0,
                    "replacement daemon exits cleanly");

  gw::test::ServerProcess ownership_server(argv[1]);
  const std::string ownership_path = ownership_server.socket_path();
  gw::test::require(::unlink(ownership_path.c_str()) == 0,
                    "unlink daemon socket before identity replacement");
  const int replacement_socket =
      ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  gw::test::require(replacement_socket >= 0, "create identity replacement");
  sockaddr_un replacement_address {};
  replacement_address.sun_family = AF_UNIX;
  std::memcpy(replacement_address.sun_path, ownership_path.c_str(),
              ownership_path.size() + 1);
  gw::test::require(
      ::bind(replacement_socket,
             reinterpret_cast<sockaddr*>(&replacement_address),
             static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) +
                                    ownership_path.size() + 1)) == 0,
      "bind identity replacement socket");
  const int ownership_status = ownership_server.stop(SIGTERM);
  gw::test::require(WIFEXITED(ownership_status) &&
                        WEXITSTATUS(ownership_status) == 0,
                    "identity test daemon exits cleanly");
  gw::test::require(::lstat(ownership_path.c_str(), &ignored) == 0 &&
                        S_ISSOCK(ignored.st_mode),
                    "shutdown leaves a replacement socket untouched");
  ::close(replacement_socket);
  gw::test::require(::unlink(ownership_path.c_str()) == 0,
                    "remove identity replacement fixture");
  return 0;
}
