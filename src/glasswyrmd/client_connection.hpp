#pragma once

#include "glasswyrmd/request_dispatcher.hpp"
#include "glasswyrmd/server_state.hpp"
#include "protocol/x11/core.hpp"
#include "protocol/x11/request.hpp"
#include "protocol/x11/setup.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace glasswyrm::server {

inline constexpr std::size_t kMaximumRequestsPerClientTurn = 64;
inline constexpr std::size_t kMaximumRequestBytesPerClientTurn = 256U * 1024U;

class RequestWorkBudget {
 public:
  [[nodiscard]] bool available() const noexcept {
    return requests_ < kMaximumRequestsPerClientTurn &&
           bytes_ < kMaximumRequestBytesPerClientTurn;
  }
  void record(std::size_t bytes) noexcept {
    ++requests_;
    bytes_ += bytes;
  }
  [[nodiscard]] std::size_t requests() const noexcept { return requests_; }
  [[nodiscard]] std::size_t bytes() const noexcept { return bytes_; }

 private:
  std::size_t requests_{0};
  std::size_t bytes_{0};
};

class ClientConnection {
 public:
  using DeferredHandler =
      std::function<bool(ClientConnection&, const DispatchResult&)>;
  using StructuralTransitionHandler =
      std::function<void(const std::vector<StructuralTransition>&)>;
  using DrawableDamageHandler =
      std::function<void(const std::vector<DrawableDamage>&)>;
  using DispatchBlockToken = std::uint64_t;
  enum class State {
    AwaitingSetup,
    Established,
    Rejecting,
    Closing,
  };

  ClientConnection(int descriptor, std::uint64_t identifier,
                   std::uint32_t resource_id_base, ServerState& server_state,
                   bool integrated_lifecycle = false,
                   DeferredHandler deferred_handler = {},
                   StructuralTransitionHandler transition_handler = {},
                   DrawableDamageHandler damage_handler = {});
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
  [[nodiscard]] gw::protocol::x11::ByteOrder byte_order() const noexcept {
    return byte_order_;
  }
  [[nodiscard]] std::uint64_t last_request_sequence() const noexcept {
    return request_sequence_;
  }
  [[nodiscard]] bool enqueue_server_packet(std::vector<std::uint8_t> bytes);
  void set_dispatch_blocked(DispatchBlockToken token) noexcept;
  [[nodiscard]] bool clear_dispatch_blocked(DispatchBlockToken token) noexcept;
  [[nodiscard]] bool dispatch_blocked() const noexcept {
    return dispatch_block_token_.has_value();
  }
  void mark_transport_closed() noexcept;
  [[nodiscard]] short poll_events() const;
  [[nodiscard]] bool needs_service() const noexcept {
    return state_ == State::Established && !dispatch_blocked() &&
           !pending_input_.empty();
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
                     RequestWorkBudget& budget);
  void process_pending(RequestWorkBudget& budget);
  void write_output();
  void prepare_setup_reply();
  void reject_framing(gw::protocol::x11::CoreErrorCode code,
                      const char* reason);
  [[nodiscard]] bool enqueue(std::vector<std::uint8_t> bytes,
                             bool close_after = false);
  void close_with_log(const char* reason);
  void close_after_output(const char* reason);

  static constexpr std::size_t kMaximumQueuedOutput = 1024U * 1024U;
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
  std::optional<DispatchBlockToken> dispatch_block_token_;
  bool integrated_lifecycle_{false};
  DeferredHandler deferred_handler_;
  StructuralTransitionHandler transition_handler_;
  DrawableDamageHandler damage_handler_;
};

}  // namespace glasswyrm::server
