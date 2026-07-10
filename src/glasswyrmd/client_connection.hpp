#pragma once

#include "protocol/x11/setup.hpp"

#include <cstdint>
#include <vector>

namespace glasswyrm::server {

class ClientConnection {
 public:
  enum class State {
    AwaitingSetup,
    WritingSetupReply,
    Established,
    Closing,
  };

  ClientConnection(int descriptor, std::uint64_t identifier,
                   std::uint32_t resource_id_base);
  ~ClientConnection();

  ClientConnection(const ClientConnection&) = delete;
  ClientConnection& operator=(const ClientConnection&) = delete;
  ClientConnection(ClientConnection&& other) noexcept;
  ClientConnection& operator=(ClientConnection&& other) noexcept;

  [[nodiscard]] int descriptor() const { return descriptor_; }
  [[nodiscard]] std::uint64_t identifier() const { return identifier_; }
  [[nodiscard]] State state() const { return state_; }
  [[nodiscard]] short poll_events() const;

  void handle_events(short events);

 private:
  void read_setup();
  void write_reply();
  void close_with_log(const char* reason);
  void prepare_reply();

  int descriptor_ = -1;
  std::uint64_t identifier_ = 0;
  std::uint32_t resource_id_base_ = 0;
  State state_ = State::AwaitingSetup;
  gw::protocol::x11::SetupParser parser_;
  std::vector<std::uint8_t> transmit_buffer_;
  std::size_t transmit_offset_ = 0;
  bool close_after_reply_ = false;
};

}  // namespace glasswyrm::server
