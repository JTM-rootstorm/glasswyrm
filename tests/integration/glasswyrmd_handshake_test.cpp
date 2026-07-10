#include "helpers/test_support.hpp"
#include "helpers/x11_fake_client.hpp"
#include "integration/server_fixture.hpp"

#include <cstdint>
#include <string>
#include <thread>

namespace {
namespace x11 = gw::protocol::x11;

std::uint32_t read_u32(const std::uint8_t* bytes, x11::ByteOrder order) {
  if (order == x11::ByteOrder::LittleEndian) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
  }
  return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
         (static_cast<std::uint32_t>(bytes[1]) << 16U) |
         (static_cast<std::uint32_t>(bytes[2]) << 8U) |
         static_cast<std::uint32_t>(bytes[3]);
}

std::vector<std::uint8_t> handshake(const std::string& path,
                                    x11::ByteOrder order,
                                    std::size_t fragment_size) {
  gw::test::X11FakeClient client(path);
  const auto request = gw::test::make_setup_request(order);
  client.send_all(request, fragment_size);
  return client.receive_setup_reply(order);
}

}  // namespace

int main(int argc, char** argv) {
  gw::test::require(argc == 2, "daemon path argument is required");
  gw::test::ServerProcess server(argv[1]);

  const auto little = handshake(server.socket_path(), x11::ByteOrder::LittleEndian,
                                static_cast<std::size_t>(-1));
  gw::test::require(little.size() > 40 && little[0] == 1,
                    "little-endian setup succeeds");

  const auto big = handshake(server.socket_path(), x11::ByteOrder::BigEndian, 3);
  gw::test::require(big.size() == little.size() && big[0] == 1,
                    "fragmented big-endian setup succeeds");

  const auto one_byte =
      handshake(server.socket_path(), x11::ByteOrder::LittleEndian, 1);
  gw::test::require(one_byte[0] == 1, "one-byte fragments succeed");

  gw::test::X11FakeClient unsupported(server.socket_path());
  unsupported.send_all(
      gw::test::make_setup_request(x11::ByteOrder::LittleEndian, 12, 0));
  gw::test::require(
      unsupported.receive_setup_reply(x11::ByteOrder::LittleEndian)[0] == 0,
      "unsupported protocol returns setup failure");
  gw::test::require(unsupported.peer_closed(),
                    "unsupported protocol connection closes");

  gw::test::X11FakeClient unsupported_minor(server.socket_path());
  unsupported_minor.send_all(
      gw::test::make_setup_request(x11::ByteOrder::BigEndian, 11, 1));
  gw::test::require(
      unsupported_minor.receive_setup_reply(x11::ByteOrder::BigEndian)[0] == 0,
      "unsupported protocol minor returns setup failure");

  constexpr std::uint8_t auth[] = {'c', 'o', 'o', 'k', 'i', 'e'};
  gw::test::X11FakeClient authorized(server.socket_path());
  authorized.send_all(gw::test::make_setup_request(
      x11::ByteOrder::BigEndian, 11, 0, auth, auth));
  gw::test::require(
      authorized.receive_setup_reply(x11::ByteOrder::BigEndian)[0] == 0,
      "authorization is explicitly rejected");

  gw::test::X11FakeClient authorization_data_only(server.socket_path());
  authorization_data_only.send_all(gw::test::make_setup_request(
      x11::ByteOrder::LittleEndian, 11, 0, {}, auth));
  gw::test::require(
      authorization_data_only
              .receive_setup_reply(x11::ByteOrder::LittleEndian)[0] == 0,
      "authorization data without a name is explicitly rejected");

  std::vector<std::uint8_t> first;
  std::vector<std::uint8_t> second;
  gw::test::X11FakeClient first_client(server.socket_path());
  gw::test::X11FakeClient second_client(server.socket_path());
  std::thread first_thread([&] {
    first_client.send_all(
        gw::test::make_setup_request(x11::ByteOrder::LittleEndian), 2);
    first = first_client.receive_setup_reply(x11::ByteOrder::LittleEndian);
  });
  std::thread second_thread([&] {
    second_client.send_all(
        gw::test::make_setup_request(x11::ByteOrder::BigEndian), 2);
    second = second_client.receive_setup_reply(x11::ByteOrder::BigEndian);
  });
  first_thread.join();
  second_thread.join();
  gw::test::require(read_u32(first.data() + 12, x11::ByteOrder::LittleEndian) !=
                        read_u32(second.data() + 12, x11::ByteOrder::BigEndian),
                    "concurrent clients receive distinct resource bases");
  return 0;
}
