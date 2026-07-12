#include "helpers/test_support.hpp"
#include "helpers/x11_fake_client.hpp"
#include "helpers/x11_request_builder.hpp"
#include "protocol/x11/lifecycle_request.hpp"

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

pid_t launch(const char *program, const std::vector<std::string> &arguments) {
  const auto child = ::fork();
  gw::test::require(child >= 0, "fork integrated process");
  if (child == 0) {
    (void)::prctl(PR_SET_PDEATHSIG, SIGTERM);
    std::vector<char *> argv{const_cast<char *>(program)};
    for (const auto &argument : arguments)
      argv.push_back(const_cast<char *>(argument.c_str()));
    argv.push_back(nullptr);
    ::execv(program, argv.data());
    _exit(127);
  }
  return child;
}
void stop(pid_t child) {
  (void)::kill(child, SIGTERM);
  int status = 0;
  gw::test::require(::waitpid(child, &status, 0) == child,
                    "reap integrated process");
}
void put16(std::vector<std::uint8_t> &bytes, std::uint16_t value,
           x11::ByteOrder order) {
  if (order == x11::ByteOrder::LittleEndian) {
    bytes.push_back(value & 0xff);
    bytes.push_back(value >> 8);
  } else {
    bytes.push_back(value >> 8);
    bytes.push_back(value & 0xff);
  }
}
void put32(std::vector<std::uint8_t> &bytes, std::uint32_t value,
           x11::ByteOrder order) {
  if (order == x11::ByteOrder::LittleEndian) {
    for (unsigned shift = 0; shift < 32; shift += 8)
      bytes.push_back(value >> shift);
  } else {
    for (int shift = 24; shift >= 0; shift -= 8)
      bytes.push_back(value >> shift);
  }
}
std::vector<std::uint8_t> window_request(gw::test::X11RequestBuilder &wire,
                                         x11::CoreOpcode opcode,
                                         std::uint32_t window,
                                         x11::ByteOrder order) {
  std::vector<std::uint8_t> body;
  put32(body, window, order);
  return wire.raw(static_cast<std::uint8_t>(opcode), 0, body);
}
std::vector<std::uint8_t> configure(gw::test::X11RequestBuilder &wire,
                                    std::uint32_t window, std::uint32_t sibling,
                                    x11::ByteOrder order) {
  constexpr std::uint16_t mask =
      x11::ConfigureX | x11::ConfigureY | x11::ConfigureWidth |
      x11::ConfigureHeight | x11::ConfigureSibling | x11::ConfigureStackMode;
  std::vector<std::uint8_t> body;
  put32(body, window, order);
  put16(body, mask, order);
  put16(body, 0, order);
  put32(body, 70, order);
  put32(body, 80, order);
  put32(body, 360, order);
  put32(body, 240, order);
  put32(body, sibling, order);
  put32(body, static_cast<std::uint32_t>(x11::CoreStackMode::Above), order);
  return wire.raw(static_cast<std::uint8_t>(x11::CoreOpcode::ConfigureWindow),
                  0, body);
}
struct Session {
  gw::test::X11FakeClient client;
  gw::test::X11RequestBuilder wire;
  x11::ByteOrder order;
  std::uint32_t base{};
  Session(const std::string &socket, x11::ByteOrder value)
      : client(socket), wire(value), order(value) {
    client.send_all(gw::test::make_setup_request(order));
    const auto setup = client.receive_setup_reply(order);
    gw::test::require(setup[0] == 1, "integrated X11 setup succeeds");
    base = gw::test::read_wire_u32(setup.data() + 12, order);
  }
  std::vector<std::uint8_t> reply() {
    for (;;) {
      auto packet = client.receive_server_packet(order);
      if (packet[0] == 1 || packet[0] == 0)
        return packet;
    }
  }
  std::vector<std::uint8_t> packet() {
    return client.receive_server_packet(order);
  }
  void sync(std::uint16_t expected_sequence = 0) {
    client.send_all(wire.get_input_focus());
    const auto packet = reply();
    if (packet[0] != 1)
      std::fprintf(stderr, "integrated lifecycle sync X error=%u opcode=%u\n",
                   static_cast<unsigned>(packet[1]),
                   static_cast<unsigned>(packet[10]));
    gw::test::require(packet[0] == 1 &&
                          (expected_sequence == 0 ||
                           gw::test::read_wire_u16(packet.data() + 2, order) ==
                               expected_sequence),
                      "lifecycle sync reply has exact sequence");
  }
};
void exercise(const std::string &socket, x11::ByteOrder order) {
  Session session(socket, order);
  const auto first = session.base + 1;
  const auto second = session.base + 2;
  constexpr std::uint32_t structure_notify = 1U << 17U;
  const std::array<std::uint32_t, 1> events{structure_notify};
  session.client.send_all(
      session.wire.create_window(first, 1, 10, 20, 320, 200));
  session.client.send_all(
      session.wire.create_window(second, 1, 30, 40, 200, 120));
  session.sync();
  session.client.send_all(
      session.wire.change_window_attributes(first, 1U << 11U, events));
  session.sync();
  session.client.send_all(
      window_request(session.wire, x11::CoreOpcode::MapWindow, first, order));
  auto event = session.packet();
  gw::test::require(
      event[0] == 19 && gw::test::read_wire_u16(event.data() + 2, order) == 6,
      "actual map emits MapNotify at the deferred request sequence");
  auto pipelined =
      window_request(session.wire, x11::CoreOpcode::MapWindow, second, order);
  const auto focus_after_map = session.wire.get_input_focus();
  pipelined.insert(pipelined.end(), focus_after_map.begin(),
                   focus_after_map.end());
  session.client.send_all(pipelined);
  const auto pipelined_reply = session.reply();
  gw::test::require(
      pipelined_reply[0] == 1 &&
          gw::test::read_wire_u16(pipelined_reply.data() + 2, order) == 8,
      "pipelined GetInputFocus resumes at its exact sequence after deferred "
      "map");
  session.client.send_all(configure(session.wire, first, second, order));
  event = session.packet();
  gw::test::require(
      event[0] == 22 && gw::test::read_wire_u16(event.data() + 2, order) == 9,
      "actual configure emits ConfigureNotify at the deferred sequence");
  session.sync(10);

  auto no_op = configure(session.wire, first, second, order);
  const auto focus_after_no_op_configure = session.wire.get_input_focus();
  no_op.insert(no_op.end(), focus_after_no_op_configure.begin(),
               focus_after_no_op_configure.end());
  session.client.send_all(no_op);
  auto no_op_reply = session.packet();
  gw::test::require(
      no_op_reply[0] == 1 &&
          gw::test::read_wire_u16(no_op_reply.data() + 2, order) == 12,
      "no-op configure emits no event before exact-sequence focus reply");

  no_op =
      window_request(session.wire, x11::CoreOpcode::MapWindow, first, order);
  const auto focus_after_no_op_map = session.wire.get_input_focus();
  no_op.insert(no_op.end(), focus_after_no_op_map.begin(),
               focus_after_no_op_map.end());
  session.client.send_all(no_op);
  no_op_reply = session.packet();
  gw::test::require(
      no_op_reply[0] == 1 &&
          gw::test::read_wire_u16(no_op_reply.data() + 2, order) == 14,
      "no-op map emits no event before exact-sequence focus reply");
  session.client.send_all(session.wire.get_geometry(first));
  auto reply = session.reply();
  gw::test::require(
      reply[0] == 1 &&
          gw::test::read_wire_u16(reply.data() + 16, order) == 360 &&
          gw::test::read_wire_u16(reply.data() + 18, order) == 240,
      "configured geometry is queryable");
  session.client.send_all(session.wire.get_window_attributes(first));
  gw::test::require(session.reply()[0] == 1, "mapped attributes are queryable");
  session.client.send_all(session.wire.query_tree(1));
  reply = session.reply();
  gw::test::require(reply[0] == 1 &&
                        gw::test::read_wire_u16(reply.data() + 16, order) == 2,
                    "restacked top-level windows remain in root tree");
  session.client.send_all(session.wire.get_input_focus());
  reply = session.reply();
  gw::test::require(reply[0] == 1, "focus is queryable after map/restack");
  session.client.send_all(
      window_request(session.wire, x11::CoreOpcode::UnmapWindow, first, order));
  event = session.packet();
  gw::test::require(
      event[0] == 18 && gw::test::read_wire_u16(event.data() + 2, order) == 19,
      "actual unmap emits UnmapNotify at the deferred request sequence");
  session.sync(20);

  no_op =
      window_request(session.wire, x11::CoreOpcode::UnmapWindow, first, order);
  const auto focus_after_no_op_unmap = session.wire.get_input_focus();
  no_op.insert(no_op.end(), focus_after_no_op_unmap.begin(),
               focus_after_no_op_unmap.end());
  session.client.send_all(no_op);
  no_op_reply = session.packet();
  gw::test::require(
      no_op_reply[0] == 1 &&
          gw::test::read_wire_u16(no_op_reply.data() + 2, order) == 22,
      "no-op unmap emits no event before exact-sequence focus reply");
  session.client.send_all(session.wire.query_tree(1));
  reply = session.reply();
  gw::test::require(reply[0] == 1 &&
                        gw::test::read_wire_u16(reply.data() + 16, order) == 2,
                    "unmapped top-level windows remain in root tree");
}
} // namespace

int main(int argc, char **argv) {
  gw::test::require(argc == 4, "expected glasswyrmd, gwm, gwcomp paths");
  char temporary[] = "/tmp/glasswyrmd-integrated-lifecycle-XXXXXX";
  gw::test::require(::mkdtemp(temporary) != nullptr,
                    "create lifecycle directory");
  const std::string root = temporary;
  const auto wm_socket = root + "/gwm.sock";
  const auto comp_socket = root + "/gwcomp.sock";
  const auto manifest = root + "/scene.jsonl";
  const auto wm = launch(argv[2], {"--ipc-socket", wm_socket});
  const auto comp =
      launch(argv[3], {"--ipc-socket", comp_socket, "--dump-dir",
                       root + "/dump", "--scene-manifest", manifest});
  const auto server =
      launch(argv[1], {"--display", "73", "--socket-dir", root, "--wm-socket",
                       wm_socket, "--compositor-socket", comp_socket});
  const auto x_socket = root + "/X73";
  for (int attempt = 0; attempt < 500 && !std::filesystem::exists(x_socket);
       ++attempt)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  gw::test::require(std::filesystem::exists(x_socket),
                    "integrated X socket becomes ready");
  exercise(x_socket, x11::ByteOrder::LittleEndian);
  exercise(x_socket, x11::ByteOrder::BigEndian);
  stop(server);
  stop(wm);
  stop(comp);
  gw::test::require(std::filesystem::exists(manifest),
                    "metadata manifest was produced");
  for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
    gw::test::require(entry.path().extension() != ".ppm",
                      "integrated lifecycle creates no PPM");
  return 0;
}
