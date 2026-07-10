#include "helpers/test_support.hpp"
#include "helpers/x11_fake_client.hpp"
#include "integration/server_fixture.hpp"

#include <cstdint>
#include <sys/socket.h>

int main(int argc, char** argv) {
  using gw::protocol::x11::ByteOrder;
  gw::test::require(argc == 2, "daemon path argument is required");
  gw::test::ServerProcess server(argv[1]);

  {
    gw::test::X11FakeClient invalid(server.socket_path());
    constexpr std::uint8_t marker[] = {'?'};
    invalid.send_all(marker);
    gw::test::require(invalid.peer_closed(), "invalid marker is closed");
  }
  {
    gw::test::X11FakeClient truncated(server.socket_path());
    const auto request = gw::test::make_setup_request(ByteOrder::LittleEndian);
    truncated.send_all(std::span<const std::uint8_t>(request).first(5));
    (void)::shutdown(truncated.descriptor(), SHUT_WR);
    gw::test::require(truncated.peer_closed(), "truncated header is closed");
  }
  {
    gw::test::X11FakeClient oversized(server.socket_path());
    constexpr std::uint8_t request[] = {
        'l', 0, 11, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0, 0};
    oversized.send_all(request);
    gw::test::require(oversized.peer_closed(), "oversized setup is closed");
  }
  {
    gw::test::X11FakeClient truncated_body(server.socket_path());
    constexpr std::uint8_t auth[] = {'a', 'b', 'c'};
    const auto request = gw::test::make_setup_request(
        ByteOrder::LittleEndian, 11, 0, auth, auth);
    truncated_body.send_all(
        std::span<const std::uint8_t>(request).first(request.size() - 2));
    (void)::shutdown(truncated_body.descriptor(), SHUT_WR);
    gw::test::require(truncated_body.peer_closed(),
                      "truncated padded authorization body is closed");
  }
  {
    constexpr std::uint8_t auth[] = {'n', 'a', 'm', 'e'};
    gw::test::X11FakeClient truncated_body(server.socket_path());
    const auto request = gw::test::make_setup_request(
        ByteOrder::LittleEndian, 11, 0, auth, {});
    truncated_body.send_all(
        std::span<const std::uint8_t>(request).first(request.size() - 1));
    (void)::shutdown(truncated_body.descriptor(), SHUT_WR);
    gw::test::require(truncated_body.peer_closed(),
                      "truncated padded authorization body is closed");
  }
  {
    gw::test::X11FakeClient extra(server.socket_path());
    auto request = gw::test::make_setup_request(ByteOrder::LittleEndian);
    request.push_back(0);
    extra.send_all(request);
    gw::test::require(extra.receive_setup_reply(ByteOrder::LittleEndian)[0] == 1,
                      "valid setup prefix receives success");
    gw::test::require(extra.peer_closed(), "post-setup bytes close connection");
  }
  {
    gw::test::X11FakeClient valid(server.socket_path());
    valid.send_all(gw::test::make_setup_request(ByteOrder::BigEndian));
    gw::test::require(valid.receive_setup_reply(ByteOrder::BigEndian)[0] == 1,
                      "valid client survives malformed peers");
  }
  return 0;
}
