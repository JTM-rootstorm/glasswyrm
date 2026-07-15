#ifndef GLASSWYRM_SRC_IPC_CONNECTION_INTERNAL_HPP
#define GLASSWYRM_SRC_IPC_CONNECTION_INTERNAL_HPP

#include "ipc/internal.hpp"
#include "ipc/wire/envelope.hpp"

#include <span>

namespace gw::ipc {

void set_closed(gwipc_connection& connection) noexcept;

gwipc_status protocol_failure(
    gwipc_connection& connection, wire::ProtocolErrorCode code,
    const wire::Envelope& offending, const char* detail,
    gwipc_status result = GWIPC_STATUS_PROTOCOL_ERROR);

bool snapshot_control(std::uint16_t type) noexcept;
gwipc_status validate_application(gwipc_connection& connection,
                                  std::uint16_t type, std::uint32_t flags,
                                  std::span<const std::uint8_t> payload,
                                  std::span<const int> fds,
                                  SnapshotState& snapshot);

gwipc_status queue_hello(gwipc_connection& connection);
gwipc_status handle_hello(gwipc_connection& connection,
                          const wire::Envelope& envelope,
                          std::span<const std::uint8_t> payload);
gwipc_status handle_welcome(gwipc_connection& connection,
                            const wire::Envelope& envelope,
                            std::span<const std::uint8_t> payload);

gwipc_status flush(gwipc_connection& connection) noexcept;
gwipc_status receive_one(gwipc_connection& connection);

gwipc_status queue_internal(gwipc_connection& connection, std::uint16_t type,
                            std::uint32_t flags, std::uint64_t reply_to,
                            std::span<const std::uint8_t> payload,
                            std::span<const int> fds = {});

gwipc_status process_poll_events(gwipc_connection& connection,
                                 short revents);
gwipc_status enqueue_with_sequence(gwipc_connection& connection,
                                   const gwipc_outgoing_message& message,
                                   std::uint64_t& out_sequence);
gwipc_status validate_incoming_reply(
    gwipc_connection& connection, const wire::Envelope& envelope,
    std::span<const std::uint8_t> payload);
gwipc_status track_incoming_request(
    gwipc_connection& connection, const wire::Envelope& envelope,
    std::span<const std::uint8_t> payload);
void rollback_incoming_request(gwipc_connection& connection,
                               const wire::Envelope& envelope) noexcept;

}  // namespace gw::ipc

#endif
