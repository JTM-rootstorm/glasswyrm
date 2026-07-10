#include "glasswyrmd/client_connection.hpp"

#include <cerrno>
#include <cstdio>
#include <poll.h>
#include <span>
#include <sys/socket.h>
#include <unistd.h>

namespace glasswyrm::server {
namespace x11 = gw::protocol::x11;

ClientConnection::ClientConnection(int descriptor, std::uint64_t identifier,
                                   std::uint32_t resource_id_base)
    : descriptor_(descriptor),
      identifier_(identifier),
      resource_id_base_(resource_id_base) {}

ClientConnection::~ClientConnection() {
  if (descriptor_ >= 0) {
    ::close(descriptor_);
  }
}

ClientConnection::ClientConnection(ClientConnection&& other) noexcept
    : descriptor_(other.descriptor_),
      identifier_(other.identifier_),
      resource_id_base_(other.resource_id_base_),
      state_(other.state_),
      parser_(std::move(other.parser_)),
      transmit_buffer_(std::move(other.transmit_buffer_)),
      transmit_offset_(other.transmit_offset_),
      close_after_reply_(other.close_after_reply_) {
  other.descriptor_ = -1;
}

ClientConnection& ClientConnection::operator=(ClientConnection&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  if (descriptor_ >= 0) {
    ::close(descriptor_);
  }
  descriptor_ = other.descriptor_;
  identifier_ = other.identifier_;
  resource_id_base_ = other.resource_id_base_;
  state_ = other.state_;
  parser_ = std::move(other.parser_);
  transmit_buffer_ = std::move(other.transmit_buffer_);
  transmit_offset_ = other.transmit_offset_;
  close_after_reply_ = other.close_after_reply_;
  other.descriptor_ = -1;
  return *this;
}

short ClientConnection::poll_events() const {
  switch (state_) {
    case State::AwaitingSetup:
    case State::Established:
      return POLLIN;
    case State::WritingSetupReply:
      return POLLOUT;
    case State::Closing:
      return 0;
  }
  return 0;
}

void ClientConnection::close_with_log(const char* reason) {
  std::fprintf(stderr, "glasswyrmd: client %llu: %s\n",
               static_cast<unsigned long long>(identifier_), reason);
  state_ = State::Closing;
}

void ClientConnection::prepare_reply() {
  const auto& request = parser_.request();
  std::fprintf(stderr,
               "glasswyrmd: client %llu: setup byte_order=%c protocol=%u.%u\n",
               static_cast<unsigned long long>(identifier_),
               static_cast<char>(request.byte_order), request.protocol_major,
               request.protocol_minor);
  switch (x11::evaluate_setup_request(request)) {
    case x11::SetupDecision::Accepted: {
      x11::SetupReplyConfig config;
      config.resource_id_base = resource_id_base_;
      config.resource_id_mask = 0x001fffffU;
      transmit_buffer_ = x11::encode_setup_success(request.byte_order, config);
      close_after_reply_ = false;
      std::fprintf(stderr, "glasswyrmd: client %llu: X11 setup accepted\n",
                   static_cast<unsigned long long>(identifier_));
      break;
    }
    case x11::SetupDecision::UnsupportedVersion:
      std::fprintf(stderr,
                   "glasswyrmd: client %llu: unsupported protocol version\n",
                   static_cast<unsigned long long>(identifier_));
      transmit_buffer_ =
          x11::encode_setup_failure(request.byte_order,
                                    "X11 protocol 11.0 required");
      close_after_reply_ = true;
      break;
    case x11::SetupDecision::UnsupportedAuthorization:
      std::fprintf(stderr,
                   "glasswyrmd: client %llu: unsupported authorization "
                   "name_length=%zu data_length=%zu\n",
                   static_cast<unsigned long long>(identifier_),
                   request.authorization_name.size(),
                   request.authorization_data.size());
      transmit_buffer_ = x11::encode_setup_failure(
          request.byte_order,
          "authorization is not supported in Milestone 1");
      close_after_reply_ = true;
      break;
  }
  state_ = State::WritingSetupReply;
}

void ClientConnection::read_setup() {
  std::uint8_t buffer[4096];
  for (;;) {
    const ssize_t size = ::recv(descriptor_, buffer, sizeof(buffer), 0);
    if (size > 0) {
      if (state_ == State::Established) {
        close_with_log("request dispatch is not implemented");
        return;
      }

      const auto input =
          std::span<const std::uint8_t>(buffer, static_cast<std::size_t>(size));
      const auto result = parser_.consume(input);
      switch (result.status) {
        case x11::ParseStatus::NeedMore:
          break;
        case x11::ParseStatus::Complete:
          prepare_reply();
          if (result.consumed != input.size()) {
            close_after_reply_ = true;
            std::fprintf(stderr,
                         "glasswyrmd: client %llu: request dispatch is not "
                         "implemented\n",
                         static_cast<unsigned long long>(identifier_));
          }
          return;
        case x11::ParseStatus::InvalidByteOrder:
          close_with_log("invalid X11 byte-order marker");
          return;
        case x11::ParseStatus::MessageTooLarge:
          close_with_log("X11 setup message exceeds configured limit");
          return;
        case x11::ParseStatus::LengthOverflow:
          close_with_log("X11 setup message length overflow");
          return;
        case x11::ParseStatus::TruncatedInput:
          close_with_log("connection closed during X11 setup");
          return;
      }
      continue;
    }
    if (size == 0) {
      if (state_ == State::AwaitingSetup) {
        const auto status = parser_.eof();
        (void)status;
        close_with_log("connection closed during X11 setup");
      } else {
        state_ = State::Closing;
      }
      return;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return;
    }
    close_with_log("socket read failed");
    return;
  }
}

void ClientConnection::write_reply() {
  while (transmit_offset_ < transmit_buffer_.size()) {
    const ssize_t size =
        ::send(descriptor_, transmit_buffer_.data() + transmit_offset_,
               transmit_buffer_.size() - transmit_offset_, MSG_NOSIGNAL);
    if (size > 0) {
      transmit_offset_ += static_cast<std::size_t>(size);
      continue;
    }
    if (size < 0 && errno == EINTR) {
      continue;
    }
    if (size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return;
    }
    close_with_log("socket write failed");
    return;
  }

  transmit_buffer_.clear();
  transmit_offset_ = 0;
  state_ = close_after_reply_ ? State::Closing : State::Established;
}

void ClientConnection::handle_events(short events) {
  if ((events & (POLLERR | POLLNVAL)) != 0) {
    close_with_log("socket error");
    return;
  }
  if (state_ == State::WritingSetupReply && (events & POLLOUT) != 0) {
    write_reply();
  } else if ((state_ == State::AwaitingSetup || state_ == State::Established) &&
             (events & POLLIN) != 0) {
    read_setup();
  }
  if ((events & POLLHUP) != 0 && state_ != State::Closing) {
    if (state_ == State::AwaitingSetup) {
      read_setup();
    } else {
      state_ = State::Closing;
    }
  }
}

}  // namespace glasswyrm::server
