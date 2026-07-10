#include "helpers/x11_fake_client.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

namespace {

struct Options {
  std::string socket_dir = "/tmp/.X11-unix";
  std::string display = "0";
  gw::protocol::x11::ByteOrder byte_order =
      gw::protocol::x11::ByteOrder::LittleEndian;
  bool malformed = false;
};

bool parse(int argc, char** argv, Options& options) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--socket-dir" && index + 1 < argc) {
      options.socket_dir = argv[++index];
    } else if (argument == "--display" && index + 1 < argc) {
      options.display = argv[++index];
      if (!options.display.empty() && options.display.front() == ':') {
        options.display.erase(0, 1);
      }
    } else if (argument == "--byte-order" && index + 1 < argc) {
      const std::string_view value(argv[++index]);
      if (value == "little") {
        options.byte_order = gw::protocol::x11::ByteOrder::LittleEndian;
      } else if (value == "big") {
        options.byte_order = gw::protocol::x11::ByteOrder::BigEndian;
      } else {
        return false;
      }
    } else if (argument == "--malformed") {
      options.malformed = true;
    } else {
      return false;
    }
  }
  return !options.display.empty() &&
         options.display.find_first_not_of("0123456789") == std::string::npos;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parse(argc, argv, options)) {
    std::cerr << "Usage: x11_setup_probe [--socket-dir PATH] [--display :N] "
                 "[--byte-order little|big | --malformed]\n";
    return 2;
  }
  try {
    gw::test::X11FakeClient client(options.socket_dir + "/X" + options.display);
    if (options.malformed) {
      constexpr std::uint8_t invalid[] = {'?', 0, 0, 11};
      client.send_all(invalid);
      if (!client.peer_closed()) {
        std::cerr << "server did not close malformed connection\n";
        return 1;
      }
      std::cout << "malformed setup rejected\n";
      return 0;
    }
    const auto request = gw::test::make_setup_request(options.byte_order);
    client.send_all(request);
    const auto reply = client.receive_setup_reply(options.byte_order);
    if (reply.empty() || reply.front() != 1) {
      std::cerr << "server returned setup failure\n";
      return 1;
    }
    std::cout << "X11 setup completed ("
              << (options.byte_order == gw::protocol::x11::ByteOrder::LittleEndian
                      ? "little"
                      : "big")
              << " endian, " << reply.size() << " bytes)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "x11_setup_probe: " << error.what() << '\n';
    return 1;
  }
}
