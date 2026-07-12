#include "glasswyrmd/client_connection.hpp"
#include "protocol/x11/reply.hpp"
#include "tests/helpers/test_support.hpp"

#include <array>
#include <cstdint>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace {
namespace x11 = gw::protocol::x11;
using gw::test::require;

struct SocketPair {
  int server{-1};
  int client{-1};
  SocketPair() {
    int descriptors[2];
    require(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, descriptors) ==
                0,
            "create deferred connection socket pair");
    server = descriptors[0];
    client = descriptors[1];
    const int flags = ::fcntl(server, F_GETFL, 0);
    require(flags >= 0 && ::fcntl(server, F_SETFL, flags | O_NONBLOCK) == 0,
            "make deferred server socket nonblocking");
  }
  ~SocketPair() {
    if (server >= 0) ::close(server);
    if (client >= 0) ::close(client);
  }
};

void establish(glasswyrm::server::ClientConnection& connection,
               const int peer) {
  constexpr std::array<std::uint8_t, 12> setup{'l', 0, 11, 0, 0, 0,
                                                0,   0, 0,  0, 0, 0};
  require(::send(peer, setup.data(), setup.size(), 0) == 12, "send setup");
  connection.handle_events(POLLIN);
  connection.handle_events(POLLOUT);
  std::array<std::uint8_t, 8192> reply{};
  require(::recv(peer, reply.data(), reply.size(), 0) > 0,
          "receive setup reply");
}

std::vector<std::uint8_t> map_then_focus(const std::uint32_t window) {
  return {8,
          0,
          2,
          0,
          static_cast<std::uint8_t>(window),
          static_cast<std::uint8_t>(window >> 8U),
          static_cast<std::uint8_t>(window >> 16U),
          static_cast<std::uint8_t>(window >> 24U),
          43,
          0,
          1,
          0};
}

glasswyrm::server::ServerState state_with_window(const std::uint64_t owner,
                                                  const std::uint32_t base,
                                                  const std::uint32_t window) {
  glasswyrm::server::ServerState state;
  glasswyrm::server::WindowCreateSpec spec;
  spec.xid = window;
  spec.parent = state.screen().root_window;
  spec.width = 64;
  spec.height = 64;
  spec.depth = state.screen().root_depth;
  spec.window_class = glasswyrm::server::WindowClass::InputOutput;
  spec.visual = state.screen().root_visual;
  require(state.resources().create_window(owner, base,
                                          state.screen().resource_id_mask,
                                          spec) ==
              glasswyrm::server::CreateWindowStatus::Success,
          "create deferred policy window");
  return state;
}

void test_pipelined_bytes_resume_once() {
  constexpr std::uint32_t base = 0x00200000U;
  constexpr std::uint32_t window = base + 1;
  auto state = state_with_window(1, base, window);
  SocketPair sockets;
  unsigned handoffs = 0;
  glasswyrm::server::ClientConnection connection(
      sockets.server, 1, base, state, true,
      [&](auto& client, const auto& result) {
        ++handoffs;
        require(result.deferred_window == window && result.deferred_map,
                "deferred handler receives map intent");
        client.set_dispatch_blocked(77);
        return true;
      });
  sockets.server = -1;
  establish(connection, sockets.client);
  const auto requests = map_then_focus(window);
  require(::send(sockets.client, requests.data(), requests.size(), 0) ==
              static_cast<ssize_t>(requests.size()),
          "send pipelined deferred requests");
  connection.handle_events(POLLIN);
  require(handoffs == 1 && connection.dispatch_blocked() &&
              connection.last_request_sequence() == 1 &&
              !connection.needs_service(),
          "deferred request blocks after consuming one sequence");
  require(connection.clear_dispatch_blocked(77) && connection.needs_service(),
          "matching completion exposes preserved pending input");
  connection.handle_events(0);
  connection.handle_events(POLLOUT);
  std::array<std::uint8_t, 32> reply{};
  require(::recv(sockets.client, reply.data(), reply.size(), MSG_WAITALL) == 32 &&
              reply[0] == 1 && reply[2] == 2 && reply[3] == 0 &&
              connection.last_request_sequence() == 2 && handoffs == 1,
          "pending request resumes once at the next exact sequence");
}

void test_full_handler_returns_error_without_blocking() {
  constexpr std::uint32_t base = 0x00400000U;
  constexpr std::uint32_t window = base + 1;
  auto state = state_with_window(2, base, window);
  SocketPair sockets;
  unsigned handoffs = 0;
  glasswyrm::server::ClientConnection connection(
      sockets.server, 2, base, state, true,
      [&](auto& client, const auto&) {
        ++handoffs;
        return client.enqueue_server_packet(x11::encode_core_error(
            client.byte_order(),
            {x11::CoreErrorCode::BadAlloc, client.last_request_sequence(), 0,
             static_cast<std::uint8_t>(x11::CoreOpcode::MapWindow), 0}));
      });
  sockets.server = -1;
  establish(connection, sockets.client);
  const auto requests = map_then_focus(window);
  require(::send(sockets.client, requests.data(), requests.size(), 0) ==
              static_cast<ssize_t>(requests.size()),
          "send queue-full simulation pipeline");
  connection.handle_events(POLLIN);
  connection.handle_events(POLLOUT);
  std::array<std::uint8_t, 64> output{};
  require(::recv(sockets.client, output.data(), output.size(), MSG_WAITALL) == 64,
          "receive queue-full error and following reply");
  require(output[0] == 0 && output[1] == 11 && output[2] == 1 &&
              output[10] == 8 && output[32] == 1 && output[34] == 2 &&
              !connection.dispatch_blocked() &&
              connection.last_request_sequence() == 2 && handoffs == 1,
          "full handler emits BadAlloc and continues without a barrier");
}

void test_clear_area_forwards_expose_intent() {
  constexpr std::uint32_t base = 0x00600000U;
  constexpr std::uint32_t window = base + 1;
  auto state = state_with_window(3, base, window);
  SocketPair sockets;
  std::vector<glasswyrm::server::ExposeIntent> intents;
  glasswyrm::server::ClientConnection connection(
      sockets.server, 3, base, state, false, {}, {}, {},
      [&](const auto& values) { intents = values; });
  sockets.server = -1;
  establish(connection, sockets.client);
  const std::array<std::uint8_t,16> clear{
      61,1,4,0,
      static_cast<std::uint8_t>(window),static_cast<std::uint8_t>(window>>8U),
      static_cast<std::uint8_t>(window>>16U),static_cast<std::uint8_t>(window>>24U),
      0,0,0,0,0,0,0,0};
  require(::send(sockets.client,clear.data(),clear.size(),0)==16,"send clear area");
  connection.handle_events(POLLIN);
  require(intents.size()==1&&intents[0].window==window&&
              intents[0].rectangle.width==64&&intents[0].rectangle.height==64,
          "clear area forwards exposure intent");
}

}  // namespace

int main() {
  test_pipelined_bytes_resume_once();
  test_full_handler_returns_error_without_blocking();
  test_clear_area_forwards_expose_intent();
  return 0;
}
