#include "glasswyrmd/peer_transport.hpp"

namespace glasswyrm::server {

PeerTransport::PeerTransport(std::string path, const gwipc_role expected_role,
                             const gwipc_capabilities capabilities,
                             std::string label)
    : path_(std::move(path)), expected_role_(expected_role),
      capabilities_(capabilities), label_(std::move(label)) {}

bool PeerTransport::connect(std::string &error) {
  disconnect();
  gwipc_connection_options options{};
  options.struct_size = sizeof(options);
  options.path = path_.c_str();
  options.local_role = GWIPC_ROLE_PROTOCOL_SERVER;
  options.acceptable_server_roles = GWIPC_ROLE_BIT(expected_role_);
  options.offered_capabilities = capabilities_;
  options.required_peer_capabilities = capabilities_;
  options.maximum_payload = GWIPC_DEFAULT_MAXIMUM_PAYLOAD;
  options.maximum_fd_count = GWIPC_DEFAULT_MAXIMUM_FDS;
  options.maximum_queued_bytes = GWIPC_DEFAULT_MAXIMUM_QUEUED_BYTES;
  options.maximum_queued_messages = GWIPC_DEFAULT_MAXIMUM_QUEUED_MESSAGES;
  options.instance_label = label_.c_str();
  const auto status = glasswyrm::ipc::Connection::connect(options, connection_);
  if (status == GWIPC_STATUS_OK || status == GWIPC_STATUS_IN_PROGRESS)
    return true;
  error = std::string("connection failed: ") + gwipc_status_string(status);
  return false;
}

bool PeerTransport::process(const short revents, std::string &error) {
  if (!connection_)
    return false;
  const auto status =
      gwipc_connection_process_poll_events(connection_.get(), revents);
  if (status != GWIPC_STATUS_OK && status != GWIPC_STATUS_WOULD_BLOCK &&
      status != GWIPC_STATUS_IN_PROGRESS) {
    error = std::string("connection processing failed: ") +
            gwipc_status_string(status);
    return false;
  }
  if (gwipc_connection_get_state(connection_.get()) ==
      GWIPC_CONNECTION_ESTABLISHED) {
    const auto peer = gwipc_connection_peer_info(connection_.get());
    if (peer.role != expected_role_ ||
        (peer.capabilities & capabilities_) != capabilities_) {
      error = "connected peer does not satisfy its role contract";
      return false;
    }
  }
  return gwipc_connection_get_state(connection_.get()) !=
         GWIPC_CONNECTION_CLOSED;
}

void PeerTransport::disconnect() noexcept { connection_ = {}; }
int PeerTransport::fd() const noexcept {
  return connection_ ? gwipc_connection_fd(connection_.get()) : -1;
}
short PeerTransport::wanted_events() const noexcept {
  return connection_ ? gwipc_connection_wanted_poll_events(connection_.get())
                     : 0;
}
bool PeerTransport::established() const noexcept {
  return connection_ && gwipc_connection_get_state(connection_.get()) ==
                            GWIPC_CONNECTION_ESTABLISHED;
}
gwipc_connection *PeerTransport::connection() const noexcept {
  return connection_.get();
}

} // namespace glasswyrm::server
