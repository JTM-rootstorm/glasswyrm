#pragma once

#include "protocol/x11/byte_order.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gw::test {

std::vector<std::uint8_t> make_setup_request(
    protocol::x11::ByteOrder order, std::uint16_t major = 11,
    std::uint16_t minor = 0, std::span<const std::uint8_t> auth_name = {},
    std::span<const std::uint8_t> auth_data = {});

class X11FakeClient {
 public:
  X11FakeClient() = default;
  explicit X11FakeClient(const std::string& socket_path);
  ~X11FakeClient();

  X11FakeClient(const X11FakeClient&) = delete;
  X11FakeClient& operator=(const X11FakeClient&) = delete;
  X11FakeClient(X11FakeClient&& other) noexcept;
  X11FakeClient& operator=(X11FakeClient&& other) noexcept;

  void connect(const std::string& socket_path);
  void send_all(std::span<const std::uint8_t> bytes,
                std::size_t fragment_size = static_cast<std::size_t>(-1));
  std::vector<std::uint8_t> receive_setup_reply(
      protocol::x11::ByteOrder order, int timeout_ms = 2000);
  std::vector<std::uint8_t> receive_server_packet(
      protocol::x11::ByteOrder order, int timeout_ms = 2000);
  [[nodiscard]] bool peer_closed(int timeout_ms = 2000) const;
  [[nodiscard]] int descriptor() const { return descriptor_; }

 private:
  int descriptor_ = -1;
};

}  // namespace gw::test
