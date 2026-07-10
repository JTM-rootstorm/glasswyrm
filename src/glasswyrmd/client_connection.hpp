#pragma once

#include "glasswyrmd/request_dispatcher.hpp"
#include "glasswyrmd/server_state.hpp"
#include "protocol/x11/core.hpp"
#include "protocol/x11/request.hpp"
#include "protocol/x11/setup.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

namespace glasswyrm::server {

class ClientConnection {
 public:
  enum class State {
    AwaitingSetup,
    Established,
    Rejecting,
    Closing,
  };

  ClientConnection(int descriptor, std::uint64_t identifier,
                   std::uint32_t resource_id_base, ServerState& server_state);
  ~ClientConnection();

  ClientConnection(const ClientConnection&) = delete;
  ClientConnection& operator=(const ClientConnection&) = delete;
  ClientConnection(ClientConnection&&) = delete;
  ClientConnection& operator=(ClientConnection&&) = delete;

  [[nodiscard]] int descriptor() const { return descriptor_; }
  [[nodiscard]] std::uint64_t identifier() const { return identifier_; }
  [[nodiscard]] std::uint32_t resource_id_base() const {
    return resource_id_base_;
  }
  [[nodiscard]] State state() const { return state_; }
  [[nodiscard]] short poll_events() const;
  [[nodiscard]] bool needs_service() const noexcept {
    return state_ == State::Established && !pending_input_.empty();
  }

  void handle_events(short events);

 private:
  struct OutputPacket {
    std::vector<std::uint8_t> bytes;
    std::size_t offset{0};
    bool close_after{false};
  };

  void read_input();
  void process_input(std::span<const std::uint8_t> input,
                     std::size_t& requests_processed,
                     std::size_t& request_bytes_processed);
  void process_pending(std::size_t& requests_processed,
                       std::size_t& request_bytes_processed);
  void write_output();
  void prepare_setup_reply();
  void reject_framing(gw::protocol::x11::CoreErrorCode code,
                      const char* reason);
  [[nodiscard]] bool enqueue(std::vector<std::uint8_t> bytes,
                             bool close_after = false);
  void close_with_log(const char* reason);
  void cleanup_resources();

  static constexpr std::size_t kMaximumQueuedOutput = 1024U * 1024U;
  static constexpr std::size_t kMaximumRequestsPerTurn = 64;
  static constexpr std::size_t kMaximumRequestBytesPerTurn = 256U * 1024U;

  int descriptor_ = -1;
  std::uint64_t identifier_ = 0;
  std::uint32_t resource_id_base_ = 0;
  ServerState& server_state_;
  State state_ = State::AwaitingSetup;
  gw::protocol::x11::SetupParser setup_parser_;
  std::optional<gw::protocol::x11::RequestFramer> request_framer_;
  gw::protocol::x11::ByteOrder byte_order_{
      gw::protocol::x11::ByteOrder::LittleEndian};
  std::uint64_t request_sequence_ = 0;
  std::deque<OutputPacket> output_queue_;
  std::size_t queued_output_bytes_ = 0;
  std::vector<std::uint8_t> pending_input_;
  bool resources_cleaned_ = false;
};

}  // namespace glasswyrm::server
