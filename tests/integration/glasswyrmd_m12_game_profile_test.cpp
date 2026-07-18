#include "helpers/test_support.hpp"
#include "helpers/x11_fake_client.hpp"
#include "helpers/x11_request_builder.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

namespace x11 = gw::protocol::x11;
using gw::test::require;

pid_t launch(const char* program, const std::vector<std::string>& arguments) {
  const auto child = ::fork();
  require(child >= 0, "fork game-profile process");
  if (child == 0) {
    (void)::prctl(PR_SET_PDEATHSIG, SIGTERM);
    std::vector<char*> argv{const_cast<char*>(program)};
    for (const auto& argument : arguments)
      argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    ::execv(program, argv.data());
    _exit(127);
  }
  return child;
}

void stop(const pid_t child) {
  (void)::kill(child, SIGTERM);
  int status = 0;
  require(::waitpid(child, &status, 0) == child,
          "reap game-profile process");
}

struct Extension {
  std::string_view name;
  std::uint8_t opcode;
  std::uint8_t first_event;
  std::uint8_t first_error;
};

constexpr std::array kExtensions{
    Extension{"BIG-REQUESTS", 128, 0, 0},
    Extension{"MIT-SHM", 129, 64, 128},
    Extension{"XFIXES", 130, 65, 129},
    Extension{"DAMAGE", 131, 66, 130},
    Extension{"RENDER", 132, 0, 131},
    Extension{"Composite", 133, 0, 0},
    Extension{"RANDR", 134, 67, 136},
};

void append_u16(std::vector<std::uint8_t>& bytes, const std::uint16_t value,
                const x11::ByteOrder order) {
  if (order == x11::ByteOrder::LittleEndian) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  } else {
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value));
  }
}

std::vector<std::uint8_t> query_extension(
    const gw::test::X11RequestBuilder& wire, const x11::ByteOrder order,
    const std::string_view name) {
  std::vector<std::uint8_t> body;
  append_u16(body, static_cast<std::uint16_t>(name.size()), order);
  body.insert(body.end(), 2, 0);
  body.insert(body.end(), name.begin(), name.end());
  while ((body.size() & 3U) != 0) body.push_back(0);
  return wire.raw(98, 0, body);
}

struct Session {
  gw::test::X11FakeClient client;
  gw::test::X11RequestBuilder wire;
  x11::ByteOrder order;
  std::uint16_t sequence{};

  Session(const std::string& socket, const x11::ByteOrder byte_order)
      : client(socket), wire(byte_order), order(byte_order) {
    client.send_all(gw::test::make_setup_request(order));
    const auto setup = client.receive_setup_reply(order);
    require(setup[0] == 1 && setup.size() == 192 && setup[28] == 1 &&
                setup[29] == 4 && setup[134] == 24 && setup[135] == 4 &&
                setup[136] == 24 && setup[168] == 1 && setup[176] == 8 &&
                setup[184] == 32,
            "actual game profile setup advertises the frozen depth layout");
  }

  void send(const std::vector<std::uint8_t>& request) {
    client.send_all(request);
    ++sequence;
  }

  std::vector<std::uint8_t> packet(const int timeout_ms = 5000) {
    return client.receive_server_packet(order, timeout_ms);
  }
};

void test_registry(const std::string& socket, const x11::ByteOrder order) {
  Session session(socket, order);
  for (const auto& extension : kExtensions) {
    session.send(query_extension(session.wire, order, extension.name));
    const auto reply = session.packet();
    require(reply[0] == 1 && reply[8] == 1 &&
                reply[9] == extension.opcode &&
                reply[10] == extension.first_event &&
                reply[11] == extension.first_error &&
                gw::test::read_wire_u16(reply.data() + 2, order) ==
                    session.sequence,
            "actual game profile exposes the stable extension registry");
  }
}

void test_recoverable_isolation(const std::string& socket) {
  Session malformed(socket, x11::ByteOrder::BigEndian);
  Session healthy(socket, x11::ByteOrder::LittleEndian);
  malformed.send(malformed.wire.raw(98, 0));
  auto packet = malformed.packet();
  require(packet[0] == 0 && packet[1] == 16 && packet[10] == 98,
          "malformed extension request receives recoverable BadLength");

  healthy.send(healthy.wire.get_input_focus());
  packet = healthy.packet(1000);
  require(packet[0] == 1 &&
              gw::test::read_wire_u16(packet.data() + 2, healthy.order) ==
                  healthy.sequence,
          "healthy client progresses after malformed peer traffic");
  malformed.send(malformed.wire.get_input_focus());
  packet = malformed.packet();
  require(packet[0] == 1 &&
              gw::test::read_wire_u16(packet.data() + 2, malformed.order) ==
                  malformed.sequence,
          "malformed client remains usable after its recoverable error");
}

std::vector<std::uint8_t> extended_no_operation(
    const x11::ByteOrder order, const std::size_t byte_size) {
  require(byte_size >= 8 && (byte_size & 3U) == 0,
          "extended request size is aligned");
  std::vector<std::uint8_t> request(byte_size, 0);
  request[0] = 127;
  const auto units = static_cast<std::uint32_t>(byte_size / 4U);
  if (order == x11::ByteOrder::LittleEndian) {
    request[4] = static_cast<std::uint8_t>(units);
    request[5] = static_cast<std::uint8_t>(units >> 8U);
    request[6] = static_cast<std::uint8_t>(units >> 16U);
    request[7] = static_cast<std::uint8_t>(units >> 24U);
  } else {
    request[4] = static_cast<std::uint8_t>(units >> 24U);
    request[5] = static_cast<std::uint8_t>(units >> 16U);
    request[6] = static_cast<std::uint8_t>(units >> 8U);
    request[7] = static_cast<std::uint8_t>(units);
  }
  return request;
}

void test_big_request_fairness(const std::string& socket) {
  Session large(socket, x11::ByteOrder::LittleEndian);
  Session small(socket, x11::ByteOrder::BigEndian);
  large.send(large.wire.raw(128, 0));
  const auto enabled = large.packet();
  require(enabled[0] == 1 &&
              gw::test::read_wire_u32(enabled.data() + 8, large.order) ==
                  4U * 1024U * 1024U,
          "BIG-REQUESTS Enable reports the 16 MiB cap");

  auto request = extended_no_operation(large.order, 4U * 1024U * 1024U);
  std::atomic<bool> started{false};
  std::exception_ptr sender_error;
  std::thread sender([&] {
    started = true;
    try {
      large.client.send_all(request, 4096);
    } catch (...) {
      sender_error = std::current_exception();
    }
  });
  while (!started) std::this_thread::yield();
  small.send(small.wire.get_input_focus());
  const auto reply = small.packet(1000);
  require(reply[0] == 1 &&
              gw::test::read_wire_u16(reply.data() + 2, small.order) ==
                  small.sequence,
          "bounded BIG request cannot starve a small peer request");
  sender.join();
  if (sender_error) std::rethrow_exception(sender_error);
  ++large.sequence;
  large.send(large.wire.get_input_focus());
  const auto synchronized = large.packet();
  require(synchronized[0] == 1 &&
              gw::test::read_wire_u16(synchronized.data() + 2, large.order) ==
                  large.sequence,
          "large request client remains synchronized after fair servicing");
}

}  // namespace

int main(int argc, char** argv) {
  require(argc == 4, "expected glasswyrmd, gwm, gwcomp paths");
  char temporary[] = "/tmp/glasswyrmd-m12-game-profile-XXXXXX";
  require(::mkdtemp(temporary) != nullptr, "create game-profile directory");
  const std::string root = temporary;
  const auto wm_socket = root + "/gwm.sock";
  const auto compositor_socket = root + "/gwcomp.sock";
  const auto wm = launch(argv[2], {"--ipc-socket", wm_socket});
  const auto compositor = launch(
      argv[3], {"--ipc-socket", compositor_socket, "--dump-dir",
                root + "/dump"});
  const auto server = launch(
      argv[1], {"--display", "75", "--socket-dir", root, "--wm-socket",
                wm_socket, "--compositor-socket", compositor_socket,
                "--game-compat", "--software-content"});
  const auto x_socket = root + "/X75";
  for (int attempt = 0; attempt < 500 && !std::filesystem::exists(x_socket);
       ++attempt)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  require(std::filesystem::exists(x_socket),
          "integrated game-profile socket becomes ready");
  test_registry(x_socket, x11::ByteOrder::LittleEndian);
  test_registry(x_socket, x11::ByteOrder::BigEndian);
  test_recoverable_isolation(x_socket);
  test_big_request_fairness(x_socket);
  stop(server);
  stop(wm);
  stop(compositor);
  return 0;
}
