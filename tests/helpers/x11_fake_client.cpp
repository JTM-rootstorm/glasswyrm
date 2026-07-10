#include "helpers/x11_fake_client.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace gw::test {
namespace {

void append_u16(std::vector<std::uint8_t>& output, std::uint16_t value,
                protocol::x11::ByteOrder order) {
  if (order == protocol::x11::ByteOrder::LittleEndian) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
  } else {
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value));
  }
}

std::uint16_t read_u16(const std::uint8_t* bytes,
                       protocol::x11::ByteOrder order) {
  if (order == protocol::x11::ByteOrder::LittleEndian) {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(bytes[1] << 8U);
  }
  return static_cast<std::uint16_t>(bytes[0] << 8U) |
         static_cast<std::uint16_t>(bytes[1]);
}

void append_padded(std::vector<std::uint8_t>& output,
                   std::span<const std::uint8_t> bytes) {
  output.insert(output.end(), bytes.begin(), bytes.end());
  while ((output.size() & 3U) != 0) {
    output.push_back(0);
  }
}

}  // namespace

std::vector<std::uint8_t> make_setup_request(
    protocol::x11::ByteOrder order, std::uint16_t major, std::uint16_t minor,
    std::span<const std::uint8_t> auth_name,
    std::span<const std::uint8_t> auth_data) {
  if (auth_name.size() > 65535 || auth_data.size() > 65535) {
    throw std::invalid_argument("authorization field is too large");
  }
  std::vector<std::uint8_t> output;
  output.reserve(12 + auth_name.size() + auth_data.size() + 6);
  output.push_back(order == protocol::x11::ByteOrder::LittleEndian ? 'l' : 'B');
  output.push_back(0);
  append_u16(output, major, order);
  append_u16(output, minor, order);
  append_u16(output, static_cast<std::uint16_t>(auth_name.size()), order);
  append_u16(output, static_cast<std::uint16_t>(auth_data.size()), order);
  append_u16(output, 0, order);
  append_padded(output, auth_name);
  append_padded(output, auth_data);
  return output;
}

X11FakeClient::X11FakeClient(const std::string& socket_path) {
  connect(socket_path);
}

X11FakeClient::~X11FakeClient() {
  if (descriptor_ >= 0) {
    ::close(descriptor_);
  }
}

X11FakeClient::X11FakeClient(X11FakeClient&& other) noexcept
    : descriptor_(other.descriptor_) {
  other.descriptor_ = -1;
}

X11FakeClient& X11FakeClient::operator=(X11FakeClient&& other) noexcept {
  if (this != &other) {
    if (descriptor_ >= 0) {
      ::close(descriptor_);
    }
    descriptor_ = other.descriptor_;
    other.descriptor_ = -1;
  }
  return *this;
}

void X11FakeClient::connect(const std::string& socket_path) {
  if (descriptor_ >= 0) {
    throw std::logic_error("client is already connected");
  }
  descriptor_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (descriptor_ < 0) {
    throw std::runtime_error(std::strerror(errno));
  }
  sockaddr_un address {};
  if (socket_path.size() >= sizeof(address.sun_path)) {
    ::close(descriptor_);
    descriptor_ = -1;
    throw std::invalid_argument("socket path is too long");
  }
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
  if (::connect(descriptor_, reinterpret_cast<sockaddr*>(&address),
                static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) +
                                       socket_path.size() + 1)) != 0) {
    const std::string message = std::strerror(errno);
    ::close(descriptor_);
    descriptor_ = -1;
    throw std::runtime_error(message);
  }
}

void X11FakeClient::send_all(std::span<const std::uint8_t> bytes,
                             std::size_t fragment_size) {
  if (fragment_size == 0) {
    throw std::invalid_argument("fragment size must be positive");
  }
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const std::size_t amount =
        std::min(fragment_size, bytes.size() - offset);
    const ssize_t sent = ::send(descriptor_, bytes.data() + offset, amount,
                                MSG_NOSIGNAL);
    if (sent > 0) {
      offset += static_cast<std::size_t>(sent);
      continue;
    }
    if (sent < 0 && errno == EINTR) {
      continue;
    }
    throw std::runtime_error(std::strerror(errno));
  }
}

std::vector<std::uint8_t> X11FakeClient::receive_setup_reply(
    protocol::x11::ByteOrder order, int timeout_ms) {
  std::vector<std::uint8_t> output;
  std::size_t expected = 8;
  while (output.size() < expected) {
    pollfd descriptor{descriptor_, POLLIN, 0};
    int result;
    do {
      result = ::poll(&descriptor, 1, timeout_ms);
    } while (result < 0 && errno == EINTR);
    if (result <= 0) {
      throw std::runtime_error(result == 0 ? "reply timeout" : std::strerror(errno));
    }
    std::uint8_t buffer[4096];
    const ssize_t size = ::recv(descriptor_, buffer,
                                std::min(sizeof(buffer), expected - output.size()),
                                0);
    if (size <= 0) {
      throw std::runtime_error("connection closed before complete setup reply");
    }
    output.insert(output.end(), buffer, buffer + size);
    if (output.size() >= 8 && expected == 8) {
      expected = 8 + static_cast<std::size_t>(read_u16(output.data() + 6, order)) * 4;
    }
  }
  return output;
}

bool X11FakeClient::peer_closed(int timeout_ms) const {
  pollfd descriptor{descriptor_, POLLIN | POLLHUP, 0};
  int result;
  do {
    result = ::poll(&descriptor, 1, timeout_ms);
  } while (result < 0 && errno == EINTR);
  if (result <= 0) {
    return false;
  }
  std::uint8_t byte = 0;
  return ::recv(descriptor_, &byte, 1, MSG_PEEK) == 0;
}

}  // namespace gw::test
