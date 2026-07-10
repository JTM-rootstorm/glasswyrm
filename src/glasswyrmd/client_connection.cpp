#include "glasswyrmd/client_connection.hpp"

#include "protocol/x11/reply.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <poll.h>
#include <span>
#include <sys/socket.h>
#include <unistd.h>

namespace glasswyrm::server {
namespace x11 = gw::protocol::x11;

ClientConnection::ClientConnection(const int descriptor,
                                   const std::uint64_t identifier,
                                   const std::uint32_t resource_id_base,
                                   ServerState& server_state)
    : descriptor_(descriptor),
      identifier_(identifier),
      resource_id_base_(resource_id_base),
      server_state_(server_state) {}

ClientConnection::~ClientConnection() {
  cleanup_resources();
  if (descriptor_ >= 0) {
    ::close(descriptor_);
  }
}

short ClientConnection::poll_events() const {
  if (state_ == State::Closing) {
    return 0;
  }
  short events = 0;
  if (state_ == State::AwaitingSetup || state_ == State::Established) {
    events |= POLLIN;
  }
  if (!output_queue_.empty()) {
    events |= POLLOUT;
  }
  return events;
}

void ClientConnection::cleanup_resources() {
  if (resources_cleaned_) {
    return;
  }
  resources_cleaned_ = true;
  const auto cleanup = server_state_.cleanup_client(identifier_);
  if (cleanup.resources_destroyed != 0 || cleanup.property_bytes_released != 0) {
    std::fprintf(stderr,
                 "glasswyrmd: client %llu: cleanup resources=%zu "
                 "property_bytes=%zu\n",
                 static_cast<unsigned long long>(identifier_),
                 cleanup.resources_destroyed, cleanup.property_bytes_released);
  }
}

void ClientConnection::close_with_log(const char* reason) {
  std::fprintf(stderr, "glasswyrmd: client %llu: %s\n",
               static_cast<unsigned long long>(identifier_), reason);
  state_ = State::Closing;
  output_queue_.clear();
  queued_output_bytes_ = 0;
  pending_input_.clear();
  cleanup_resources();
}

void ClientConnection::close_after_output(const char* reason) {
  std::fprintf(stderr, "glasswyrmd: client %llu: %s\n",
               static_cast<unsigned long long>(identifier_), reason);
  pending_input_.clear();
  if (output_queue_.empty()) {
    state_ = State::Closing;
    cleanup_resources();
    return;
  }
  output_queue_.back().close_after = true;
  state_ = State::Rejecting;
}

bool ClientConnection::enqueue(std::vector<std::uint8_t> bytes,
                               const bool close_after) {
  if (bytes.size() > kMaximumQueuedOutput - queued_output_bytes_) {
    close_with_log("output queue limit exceeded");
    return false;
  }
  queued_output_bytes_ += bytes.size();
  output_queue_.push_back({std::move(bytes), 0, close_after});
  return true;
}

void ClientConnection::prepare_setup_reply() {
  const auto& request = setup_parser_.request();
  byte_order_ = request.byte_order;
  std::fprintf(stderr,
               "glasswyrmd: client %llu: setup byte_order=%c protocol=%u.%u\n",
               static_cast<unsigned long long>(identifier_),
               static_cast<char>(request.byte_order), request.protocol_major,
               request.protocol_minor);
  switch (x11::evaluate_setup_request(request)) {
    case x11::SetupDecision::Accepted: {
      x11::SetupReplyConfig config;
      config.resource_id_base = resource_id_base_;
      config.resource_id_mask = server_state_.screen().resource_id_mask;
      (void)enqueue(x11::encode_setup_success(request.byte_order, config));
      request_framer_.emplace(request.byte_order,
                              server_state_.screen().maximum_request_length);
      state_ = State::Established;
      std::fprintf(stderr, "glasswyrmd: client %llu: X11 setup accepted\n",
                   static_cast<unsigned long long>(identifier_));
      break;
    }
    case x11::SetupDecision::UnsupportedVersion:
      (void)enqueue(x11::encode_setup_failure(request.byte_order,
                                              "X11 protocol 11.0 required"),
                    true);
      state_ = State::Rejecting;
      break;
    case x11::SetupDecision::UnsupportedAuthorization:
      (void)enqueue(x11::encode_setup_failure(
                        request.byte_order,
                        "authorization is not supported in Milestone 2"),
                    true);
      state_ = State::Rejecting;
      break;
  }
}

void ClientConnection::reject_framing(const x11::CoreErrorCode code,
                                      const char* reason) {
  ++request_sequence_;
  std::fprintf(stderr, "glasswyrmd: client %llu: %s\n",
               static_cast<unsigned long long>(identifier_), reason);
  (void)enqueue(x11::encode_core_error(
                    byte_order_,
                    {code, request_sequence_, 0,
                     static_cast<std::uint8_t>(
                         request_framer_ ? request_framer_->request().opcode
                                         : 0),
                     0}),
                true);
  state_ = State::Rejecting;
  pending_input_.clear();
}

void ClientConnection::process_input(
    const std::span<const std::uint8_t> input,
    std::size_t& requests_processed, std::size_t& request_bytes_processed) {
  std::size_t consumed = 0;
  while (consumed < input.size() && state_ != State::Closing &&
         state_ != State::Rejecting) {
    if (state_ == State::AwaitingSetup) {
      const auto result = setup_parser_.consume(input.subspan(consumed));
      consumed += result.consumed;
      switch (result.status) {
        case x11::ParseStatus::NeedMore: break;
        case x11::ParseStatus::Complete: prepare_setup_reply(); break;
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

    if (requests_processed >= kMaximumRequestsPerTurn ||
        request_bytes_processed >= kMaximumRequestBytesPerTurn) {
      pending_input_.insert(pending_input_.end(), input.begin() + consumed,
                            input.end());
      return;
    }
    const auto result = request_framer_->consume(input.subspan(consumed));
    consumed += result.consumed;
    switch (result.status) {
      case x11::RequestFrameStatus::NeedMore: break;
      case x11::RequestFrameStatus::Complete: {
        ++request_sequence_;
        ++requests_processed;
        request_bytes_processed += request_framer_->request().bytes.size();
        const DispatchContext context{identifier_, resource_id_base_,
                                      server_state_.screen().resource_id_mask,
                                      request_sequence_, byte_order_};
        auto result_packet =
            dispatch_request(server_state_, context, request_framer_->request());
        if (!result_packet.output.empty() &&
            !enqueue(std::move(result_packet.output))) {
          return;
        }
        request_framer_->reset();
        break;
      }
      case x11::RequestFrameStatus::ZeroLength:
        reject_framing(x11::CoreErrorCode::BadLength,
                       "zero-length core request");
        return;
      case x11::RequestFrameStatus::TooLarge:
        reject_framing(x11::CoreErrorCode::BadLength,
                       "core request exceeds advertised maximum");
        return;
      case x11::RequestFrameStatus::TruncatedInput:
        close_with_log("connection closed during core request");
        return;
    }
  }
}

void ClientConnection::process_pending(std::size_t& requests_processed,
                                       std::size_t& request_bytes_processed) {
  if (pending_input_.empty()) {
    return;
  }
  auto pending = std::move(pending_input_);
  pending_input_.clear();
  process_input(pending, requests_processed, request_bytes_processed);
}

void ClientConnection::read_input() {
  std::size_t requests_processed = 0;
  std::size_t request_bytes_processed = 0;
  process_pending(requests_processed, request_bytes_processed);
  if (needs_service() || state_ == State::Closing || state_ == State::Rejecting) {
    return;
  }

  std::uint8_t buffer[4096];
  for (;;) {
    const ssize_t size = ::recv(descriptor_, buffer, sizeof(buffer), 0);
    if (size > 0) {
      process_input(std::span<const std::uint8_t>(
                        buffer, static_cast<std::size_t>(size)),
                    requests_processed, request_bytes_processed);
      if (state_ == State::Closing || state_ == State::Rejecting ||
          needs_service() || requests_processed >= kMaximumRequestsPerTurn ||
          request_bytes_processed >= kMaximumRequestBytesPerTurn) {
        return;
      }
      continue;
    }
    if (size == 0) {
      if (state_ == State::AwaitingSetup) {
        (void)setup_parser_.eof();
        close_with_log("connection closed during X11 setup");
      } else if (state_ == State::Established && request_framer_ &&
                 request_framer_->eof() ==
                     x11::RequestFrameStatus::TruncatedInput) {
        close_with_log("connection closed during partial core request");
      } else {
        close_after_output("client closed its request stream");
      }
      return;
    }
    if (errno == EINTR) continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return;
    close_with_log("socket read failed");
    return;
  }
}

void ClientConnection::write_output() {
  while (!output_queue_.empty()) {
    auto& packet = output_queue_.front();
    while (packet.offset < packet.bytes.size()) {
      const ssize_t size = ::send(descriptor_, packet.bytes.data() + packet.offset,
                                  packet.bytes.size() - packet.offset,
                                  MSG_NOSIGNAL);
      if (size > 0) {
        packet.offset += static_cast<std::size_t>(size);
        queued_output_bytes_ -= static_cast<std::size_t>(size);
        continue;
      }
      if (size < 0 && errno == EINTR) continue;
      if (size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
      close_with_log("socket write failed");
      return;
    }
    const bool close_after = packet.close_after;
    output_queue_.pop_front();
    if (close_after) {
      close_with_log("rejected client response sent");
      return;
    }
  }
}

void ClientConnection::handle_events(const short events) {
  if ((events & (POLLERR | POLLNVAL)) != 0) {
    close_with_log("socket error");
    return;
  }
  if (needs_service() ||
      (((events & (POLLIN | POLLHUP)) != 0) &&
       (state_ == State::AwaitingSetup || state_ == State::Established))) {
    read_input();
  }
  if ((events & POLLOUT) != 0 && state_ != State::Closing) {
    write_output();
  }
}

}  // namespace glasswyrm::server
