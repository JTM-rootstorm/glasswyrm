#include "helpers/synthetic_input_client.hpp"

#include <poll.h>

#include <cerrno>
#include <memory>
#include <stdexcept>

namespace gw::test {
namespace {
struct PayloadDeleter {
  void operator()(gwipc_contract_payload* value) const {
    gwipc_contract_payload_destroy(value);
  }
};
struct MessageDeleter {
  void operator()(gwipc_message* value) const { gwipc_message_destroy(value); }
};
struct ContractDeleter {
  void operator()(gwipc_decoded_contract* value) const {
    gwipc_decoded_contract_destroy(value);
  }
};

bool pump(gwipc_connection* connection, int timeout_ms) {
  pollfd descriptor{gwipc_connection_fd(connection),
                    gwipc_connection_wanted_poll_events(connection), 0};
  const int ready = ::poll(&descriptor, 1, timeout_ms);
  if (ready < 0 && errno == EINTR) return true;
  if (ready < 0) return false;
  if (ready > 0 && gwipc_connection_process_poll_events(
                       connection, descriptor.revents) ==
                       GWIPC_STATUS_SYSTEM_ERROR)
    return false;
  return gwipc_connection_get_state(connection) != GWIPC_CONNECTION_CLOSED;
}

template <class Value, class Encoder>
void enqueue(gwipc_connection* connection, std::uint16_t type,
             const Value& value, Encoder encoder) {
  gwipc_contract_payload* raw = nullptr;
  if (encoder(&value, &raw) != GWIPC_STATUS_OK)
    throw std::runtime_error("synthetic input encoding failed");
  const std::unique_ptr<gwipc_contract_payload, PayloadDeleter> payload(raw);
  std::size_t size = 0;
  const auto* bytes = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.flags = GWIPC_FLAG_ACK_REQUIRED;
  message.payload = bytes;
  message.payload_size = size;
  if (gwipc_connection_enqueue(connection, &message) != GWIPC_STATUS_OK)
    throw std::runtime_error("synthetic input enqueue failed");
}
}  // namespace

void SyntheticInputClient::ConnectionDeleter::operator()(
    gwipc_connection* value) const {
  gwipc_connection_destroy(value);
}

SyntheticInputClient::SyntheticInputClient(const std::string& socket_path) {
  gwipc_connection_options options{};
  options.struct_size = sizeof(options);
  options.path = socket_path.c_str();
  options.local_role = GWIPC_ROLE_DIAGNOSTIC_TOOL;
  options.acceptable_server_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_PROTOCOL_SERVER);
  options.offered_capabilities = GWIPC_CAP_SYNTHETIC_INPUT;
  options.required_peer_capabilities = GWIPC_CAP_SYNTHETIC_INPUT;
  options.instance_label = "m8-input-probe";
  gwipc_connection* raw = nullptr;
  const auto status = gwipc_connection_connect(&options, &raw);
  if (status != GWIPC_STATUS_OK && status != GWIPC_STATUS_IN_PROGRESS)
    throw std::runtime_error("synthetic input connection failed");
  connection_.reset(raw);
  for (int attempt = 0; attempt != 200 &&
       gwipc_connection_get_state(connection_.get()) !=
           GWIPC_CONNECTION_ESTABLISHED;
       ++attempt)
    if (!pump(connection_.get(), 50))
      throw std::runtime_error("synthetic input handshake failed");
  if (gwipc_connection_get_state(connection_.get()) !=
      GWIPC_CONNECTION_ESTABLISHED)
    throw std::runtime_error("synthetic input handshake timed out");
}

SyntheticInputClient::~SyntheticInputClient() = default;
SyntheticInputClient::SyntheticInputClient(SyntheticInputClient&&) noexcept =
    default;
SyntheticInputClient& SyntheticInputClient::operator=(
    SyntheticInputClient&&) noexcept = default;

gwipc_synthetic_input_acknowledged SyntheticInputClient::motion(
    const std::uint64_t id, const std::uint32_t time, const std::int32_t x,
    const std::int32_t y) {
  gwipc_synthetic_motion value{};
  value.struct_size = sizeof(value); value.input_id = id; value.time_ms = time;
  value.root_x = x; value.root_y = y;
  enqueue(connection_.get(), GWIPC_MESSAGE_SYNTHETIC_MOTION, value,
          gwipc_contract_encode_synthetic_motion);
  return receive_ack(id);
}

gwipc_synthetic_input_acknowledged SyntheticInputClient::button(
    const std::uint64_t id, const std::uint32_t time,
    const std::uint8_t number, const bool pressed) {
  gwipc_synthetic_button value{};
  value.struct_size = sizeof(value); value.input_id = id; value.time_ms = time;
  value.button = number; value.pressed = pressed ? 1 : 0;
  enqueue(connection_.get(), GWIPC_MESSAGE_SYNTHETIC_BUTTON, value,
          gwipc_contract_encode_synthetic_button);
  return receive_ack(id);
}

gwipc_synthetic_input_acknowledged SyntheticInputClient::key(
    const std::uint64_t id, const std::uint32_t time,
    const std::uint8_t keycode, const bool pressed) {
  gwipc_synthetic_key value{};
  value.struct_size = sizeof(value); value.input_id = id; value.time_ms = time;
  value.keycode = keycode; value.pressed = pressed ? 1 : 0;
  enqueue(connection_.get(), GWIPC_MESSAGE_SYNTHETIC_KEY, value,
          gwipc_contract_encode_synthetic_key);
  return receive_ack(id);
}

gwipc_synthetic_input_acknowledged SyntheticInputClient::barrier(
    const std::uint64_t id) {
  gwipc_synthetic_barrier value{};
  value.struct_size = sizeof(value); value.input_id = id;
  enqueue(connection_.get(), GWIPC_MESSAGE_SYNTHETIC_BARRIER, value,
          gwipc_contract_encode_synthetic_barrier);
  return receive_ack(id);
}

gwipc_synthetic_input_acknowledged SyntheticInputClient::receive_ack(
    const std::uint64_t expected_input_id) {
  for (int attempt = 0; attempt != 400; ++attempt) {
    if (!pump(connection_.get(), 50))
      throw std::runtime_error("synthetic input peer disconnected");
    gwipc_message* raw_message = nullptr;
    if (gwipc_connection_receive(connection_.get(), &raw_message) !=
        GWIPC_STATUS_OK)
      continue;
    const std::unique_ptr<gwipc_message, MessageDeleter> message(raw_message);
    if (gwipc_message_type(message.get()) !=
            GWIPC_MESSAGE_SYNTHETIC_INPUT_ACKNOWLEDGED ||
        gwipc_message_flags(message.get()) != GWIPC_FLAG_REPLY)
      throw std::runtime_error("unexpected synthetic input reply");
    gwipc_decoded_contract* raw_contract = nullptr;
    if (gwipc_contract_decode_message(message.get(), &raw_contract) !=
        GWIPC_STATUS_OK)
      throw std::runtime_error("synthetic input reply decode failed");
    const std::unique_ptr<gwipc_decoded_contract, ContractDeleter> contract(
        raw_contract);
    const auto* ack = gwipc_decoded_synthetic_input_acknowledged(contract.get());
    if (ack == nullptr || ack->input_id != expected_input_id)
      throw std::runtime_error("synthetic input reply correlation failed");
    return *ack;
  }
  throw std::runtime_error("synthetic input acknowledgement timed out");
}

}  // namespace gw::test
