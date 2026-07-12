#include "glasswyrmd/synthetic_input_peer.hpp"

#include <cstdio>
#include <poll.h>
#include <utility>

namespace glasswyrm::server {
namespace {
constexpr std::size_t kMaximumRecords = 4096;

std::string status_error(const char* operation, const gwipc_status status) {
  return std::string(operation) + ": " + gwipc_status_string(status);
}
}  // namespace

void SyntheticInputPeer::ListenerDeleter::operator()(
    gwipc_listener* value) const noexcept {
  gwipc_listener_destroy(value);
}

void SyntheticInputPeer::ConnectionDeleter::operator()(
    gwipc_connection* value) const noexcept {
  gwipc_connection_destroy(value);
}

SyntheticInputPeer::SyntheticInputPeer(std::string path)
    : path_(std::move(path)) {}

SyntheticInputPeer::~SyntheticInputPeer() = default;

bool SyntheticInputPeer::start(std::string& error) {
  gwipc_listener_options options{};
  options.struct_size = sizeof(options);
  options.path = path_.c_str();
  options.local_role = GWIPC_ROLE_PROTOCOL_SERVER;
  options.accepted_peer_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_DIAGNOSTIC_TOOL);
  options.offered_capabilities = GWIPC_CAP_SYNTHETIC_INPUT;
  options.required_peer_capabilities = GWIPC_CAP_SYNTHETIC_INPUT;
  options.maximum_payload = 4096;
  options.maximum_fd_count = 0;
  options.require_same_uid = 1;
  options.maximum_queued_bytes = GWIPC_DEFAULT_MAXIMUM_QUEUED_BYTES;
  options.maximum_queued_messages = GWIPC_DEFAULT_MAXIMUM_QUEUED_MESSAGES;
  options.instance_label = "glasswyrmd-m8-input";
  gwipc_listener* raw = nullptr;
  const auto status = gwipc_listener_create(&options, &raw);
  listener_.reset(raw);
  if (status != GWIPC_STATUS_OK) {
    error = status_error("synthetic input listener creation failed", status);
    return false;
  }
  std::fprintf(stderr, "glasswyrmd: synthetic input listening socket=%s\n",
               path_.c_str());
  return true;
}

int SyntheticInputPeer::listener_fd() const noexcept {
  return listener_ ? gwipc_listener_fd(listener_.get()) : -1;
}

short SyntheticInputPeer::listener_events() const noexcept {
  return listener_ && !connection_ ? POLLIN : 0;
}

int SyntheticInputPeer::connection_fd() const noexcept {
  return connection_ ? gwipc_connection_fd(connection_.get()) : -1;
}

short SyntheticInputPeer::connection_events() const noexcept {
  return connection_ ? gwipc_connection_wanted_poll_events(connection_.get())
                     : 0;
}

void SyntheticInputPeer::accept_provider() {
  if (!listener_ || connection_) return;
  gwipc_connection* raw = nullptr;
  if (gwipc_listener_accept(listener_.get(), &raw) != GWIPC_STATUS_OK) return;
  connection_.reset(raw);
  const auto peer = gwipc_connection_peer_info(connection_.get());
  std::fprintf(stderr,
               "glasswyrmd: input provider connected pid=%d uid=%u\n",
               peer.pid, peer.uid);
}

void SyntheticInputPeer::service(const short revents) {
  if (!connection_) return;
  if (revents != 0)
    (void)gwipc_connection_process_poll_events(connection_.get(), revents);
  drain();
  if (connection_ && gwipc_connection_get_state(connection_.get()) ==
                         GWIPC_CONNECTION_CLOSED)
    disconnect();
}

void SyntheticInputPeer::drain() {
  while (connection_ && records_.size() < kMaximumRecords) {
    gwipc_message* message = nullptr;
    if (gwipc_connection_receive(connection_.get(), &message) !=
        GWIPC_STATUS_OK)
      return;
    std::unique_ptr<gwipc_message, decltype(&gwipc_message_destroy)> owned(
        message, gwipc_message_destroy);
    gwipc_decoded_contract* decoded = nullptr;
    if (gwipc_contract_decode_message(message, &decoded) != GWIPC_STATUS_OK) {
      disconnect();
      return;
    }
    std::unique_ptr<gwipc_decoded_contract,
                    decltype(&gwipc_decoded_contract_destroy)>
        contract(decoded, gwipc_decoded_contract_destroy);
    SyntheticInputRecord record{};
    record.sequence = gwipc_message_sequence(message);
    switch (gwipc_message_type(message)) {
      case GWIPC_MESSAGE_SYNTHETIC_MOTION: {
        const auto* value = gwipc_decoded_synthetic_motion(decoded);
        if (!value) { disconnect(); return; }
        record.kind = SyntheticInputRecord::Kind::Motion;
        record.input_id = value->input_id;
        record.time_ms = value->time_ms;
        record.root_x = value->root_x;
        record.root_y = value->root_y;
        break;
      }
      case GWIPC_MESSAGE_SYNTHETIC_BUTTON: {
        const auto* value = gwipc_decoded_synthetic_button(decoded);
        if (!value) { disconnect(); return; }
        record.kind = SyntheticInputRecord::Kind::Button;
        record.input_id = value->input_id;
        record.time_ms = value->time_ms;
        record.detail = value->button;
        record.pressed = value->pressed != 0;
        break;
      }
      case GWIPC_MESSAGE_SYNTHETIC_KEY: {
        const auto* value = gwipc_decoded_synthetic_key(decoded);
        if (!value) { disconnect(); return; }
        record.kind = SyntheticInputRecord::Kind::Key;
        record.input_id = value->input_id;
        record.time_ms = value->time_ms;
        record.detail = value->keycode;
        record.pressed = value->pressed != 0;
        break;
      }
      case GWIPC_MESSAGE_SYNTHETIC_BARRIER: {
        const auto* value = gwipc_decoded_synthetic_barrier(decoded);
        if (!value) { disconnect(); return; }
        record.kind = SyntheticInputRecord::Kind::Barrier;
        record.input_id = value->input_id;
        break;
      }
      default:
        disconnect();
        return;
    }
    records_.push_back(record);
  }
}

std::optional<SyntheticInputRecord> SyntheticInputPeer::take_record() {
  if (records_.empty()) return std::nullopt;
  auto record = records_.front();
  records_.pop_front();
  return record;
}

bool SyntheticInputPeer::acknowledge(
    const SyntheticInputRecord& record,
    const gwipc_synthetic_input_acknowledged& acknowledgement) {
  if (!connection_) return false;
  gwipc_contract_payload* raw = nullptr;
  if (gwipc_contract_encode_synthetic_input_acknowledged(&acknowledgement,
                                                          &raw) !=
      GWIPC_STATUS_OK)
    return false;
  std::unique_ptr<gwipc_contract_payload,
                  decltype(&gwipc_contract_payload_destroy)>
      payload(raw, gwipc_contract_payload_destroy);
  std::size_t size = 0;
  const auto* bytes = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message outgoing{};
  outgoing.struct_size = sizeof(outgoing);
  outgoing.type = GWIPC_MESSAGE_SYNTHETIC_INPUT_ACKNOWLEDGED;
  outgoing.flags = GWIPC_FLAG_REPLY;
  outgoing.reply_to = record.sequence;
  outgoing.payload = bytes;
  outgoing.payload_size = size;
  return gwipc_connection_enqueue(connection_.get(), &outgoing) ==
         GWIPC_STATUS_OK;
}

bool SyntheticInputPeer::connected() const noexcept { return connection_ != nullptr; }

bool SyntheticInputPeer::consume_disconnect() noexcept {
  return std::exchange(disconnected_, false);
}

void SyntheticInputPeer::disconnect() noexcept {
  if (!connection_) return;
  records_.clear();
  connection_.reset();
  disconnected_ = true;
  std::fprintf(stderr, "glasswyrmd: input provider disconnected\n");
}

}  // namespace glasswyrm::server
