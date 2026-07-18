#include "helpers/test_support.hpp"
#include "helpers/x11_fake_client.hpp"
#include "helpers/x11_request_builder.hpp"

#include <array>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <string>
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
  require(child >= 0, "fork integrated property process");
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
          "reap integrated property process");
}

std::array<std::uint8_t, 4> value32(const std::uint32_t value,
                                    const x11::ByteOrder order) {
  if (order == x11::ByteOrder::LittleEndian)
    return {static_cast<std::uint8_t>(value),
            static_cast<std::uint8_t>(value >> 8U),
            static_cast<std::uint8_t>(value >> 16U),
            static_cast<std::uint8_t>(value >> 24U)};
  return {static_cast<std::uint8_t>(value >> 24U),
          static_cast<std::uint8_t>(value >> 16U),
          static_cast<std::uint8_t>(value >> 8U),
          static_cast<std::uint8_t>(value)};
}

struct Session {
  gw::test::X11FakeClient client;
  gw::test::X11RequestBuilder wire;
  x11::ByteOrder order;
  std::uint32_t base{};
  std::uint16_t sequence{};

  Session(const std::string& socket, const x11::ByteOrder byte_order)
      : client(socket), wire(byte_order), order(byte_order) {
    client.send_all(gw::test::make_setup_request(order));
    const auto setup = client.receive_setup_reply(order);
    require(setup[0] == 1 && setup.size() == 192,
            "integrated game-profile setup succeeds");
    base = gw::test::read_wire_u32(setup.data() + 12, order);
  }

  void send(const std::vector<std::uint8_t>& request) {
    client.send_all(request);
    ++sequence;
  }

  std::vector<std::uint8_t> packet() {
    return client.receive_server_packet(order);
  }

  std::vector<std::uint8_t> reply() {
    for (;;) {
      auto result = packet();
      if (result[0] == 0 || result[0] == 1) return result;
    }
  }

  void sync() {
    send(wire.get_input_focus());
    const auto result = reply();
    require(result[0] == 1 &&
                gw::test::read_wire_u16(result.data() + 2, order) == sequence,
            "integrated property sync has the exact sequence");
  }

  std::uint32_t intern(const std::string_view name) {
    send(wire.intern_atom(name));
    const auto result = reply();
    require(result[0] == 1 &&
                gw::test::read_wire_u16(result.data() + 2, order) == sequence,
            "integrated property atom lookup succeeds");
    return gw::test::read_wire_u32(result.data() + 8, order);
  }
};

void exercise(const std::string& socket, const x11::ByteOrder order) {
  Session session(socket, order);
  const auto window = session.base + 1;
  session.send(session.wire.create_window(window, 1, 10, 20, 320, 200));
  session.sync();
  constexpr std::uint32_t property_change_mask = 1U << 22U;
  const std::array<std::uint32_t, 1> events{property_change_mask};
  session.send(session.wire.change_window_attributes(
      window, 1U << 11U, events));
  session.sync();

  const auto bypass = session.intern("_NET_WM_BYPASS_COMPOSITOR");
  const auto enabled = value32(1, order);
  auto pipeline = session.wire.change_property(
      0, window, bypass, 6, 32, enabled, 1);
  const auto focus = session.wire.get_input_focus();
  pipeline.insert(pipeline.end(), focus.begin(), focus.end());
  session.client.send_all(pipeline);
  const auto property_sequence = ++session.sequence;
  const auto focus_sequence = ++session.sequence;
  auto packet = session.packet();
  require(packet[0] == 28 &&
              gw::test::read_wire_u16(packet.data() + 2, order) ==
                  property_sequence &&
              gw::test::read_wire_u32(packet.data() + 4, order) == window &&
              gw::test::read_wire_u32(packet.data() + 8, order) == bypass &&
              gw::test::read_wire_u32(packet.data() + 12, order) == 1 &&
              packet[16] == 0,
          "accepted policy-backed ChangeProperty emits NewValue with the "
          "request input time only at lifecycle completion");
  packet = session.packet();
  require(packet[0] == 1 &&
              gw::test::read_wire_u16(packet.data() + 2, order) ==
                  focus_sequence,
          "accepted property lifecycle resumes the pipelined request");

  constexpr std::uint32_t wm_transient_for = 68;
  constexpr std::uint32_t window_type = 33;
  const auto unknown = value32(session.base + 0x1000U, order);
  pipeline = session.wire.change_property(
      0, window, wm_transient_for, window_type, 32, unknown, 1);
  pipeline.insert(pipeline.end(), focus.begin(), focus.end());
  session.client.send_all(pipeline);
  ++session.sequence;
  const auto rejected_focus_sequence = ++session.sequence;
  packet = session.packet();
  require(packet[0] == 1 &&
              gw::test::read_wire_u16(packet.data() + 2, order) ==
                  rejected_focus_sequence,
          "policy rejection emits no PropertyNotify and resumes the client");

  session.send(session.wire.get_property(window, wm_transient_for, 0, 0, 1));
  packet = session.packet();
  require(packet[0] == 1 &&
              gw::test::read_wire_u16(packet.data() + 2, order) ==
                  session.sequence &&
              packet[1] == 0 &&
              gw::test::read_wire_u32(packet.data() + 8, order) == 0,
          "rejected policy property is neither notified nor committed");
}

}  // namespace

int main(int argc, char** argv) {
  require(argc == 4, "expected glasswyrmd, gwm, gwcomp paths");
  char temporary[] = "/tmp/glasswyrmd-integrated-property-XXXXXX";
  require(::mkdtemp(temporary) != nullptr,
          "create integrated property directory");
  const std::string root = temporary;
  const auto wm_socket = root + "/gwm.sock";
  const auto compositor_socket = root + "/gwcomp.sock";
  const auto wm = launch(argv[2], {"--ipc-socket", wm_socket});
  const auto compositor = launch(
      argv[3], {"--ipc-socket", compositor_socket, "--dump-dir",
                root + "/dump"});
  const auto server = launch(
      argv[1], {"--display", "74", "--socket-dir", root, "--wm-socket",
                wm_socket, "--compositor-socket", compositor_socket,
                "--game-compat", "--software-content"});
  const auto x_socket = root + "/X74";
  for (int attempt = 0; attempt < 500 && !std::filesystem::exists(x_socket);
       ++attempt)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  require(std::filesystem::exists(x_socket),
          "integrated game-profile socket becomes ready");
  exercise(x_socket, x11::ByteOrder::LittleEndian);
  exercise(x_socket, x11::ByteOrder::BigEndian);
  stop(server);
  stop(wm);
  stop(compositor);
  return 0;
}
