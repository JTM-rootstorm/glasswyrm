#include "glasswyrmd/policy_peer.hpp"

#include <memory>

namespace glasswyrm::server {
namespace {
constexpr gwipc_capabilities kCapabilities =
    GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_WINDOW_POLICY | GWIPC_CAP_WINDOW_LIFECYCLE;
struct ContractDelete {
  void operator()(gwipc_contract_payload *p) const {
    gwipc_contract_payload_destroy(p);
  }
};
struct ControlDelete {
  void operator()(gwipc_control_payload *p) const {
    gwipc_control_payload_destroy(p);
  }
};
struct DecodedContractDelete {
  void operator()(gwipc_decoded_contract *p) const {
    gwipc_decoded_contract_destroy(p);
  }
};
struct DecodedControlDelete {
  void operator()(gwipc_decoded_control *p) const {
    gwipc_decoded_control_destroy(p);
  }
};

template <class T, class Encoder>
bool enqueue_contract(gwipc_connection *connection, std::uint16_t type,
                      std::uint32_t flags, const T &value, Encoder encoder) {
  gwipc_contract_payload *raw = nullptr;
  if (encoder(&value, &raw) != GWIPC_STATUS_OK)
    return false;
  std::unique_ptr<gwipc_contract_payload, ContractDelete> payload(raw);
  std::size_t size = 0;
  const auto *data = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.flags = flags;
  message.payload = data;
  message.payload_size = size;
  return gwipc_connection_enqueue(connection, &message) == GWIPC_STATUS_OK;
}
template <class T, class Encoder>
bool enqueue_control(gwipc_connection *connection, std::uint16_t type,
                     const T &value, Encoder encoder) {
  gwipc_control_payload *raw = nullptr;
  if (encoder(&value, &raw) != GWIPC_STATUS_OK)
    return false;
  std::unique_ptr<gwipc_control_payload, ControlDelete> payload(raw);
  std::size_t size = 0;
  const auto *data = gwipc_control_payload_data(payload.get(), &size);
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.payload = data;
  message.payload_size = size;
  return gwipc_connection_enqueue(connection, &message) == GWIPC_STATUS_OK;
}
} // namespace

PolicyPeer::PolicyPeer(std::string path,
                       const gw::protocol::x11::ScreenModel screen)
    : transport_(std::move(path), GWIPC_ROLE_WINDOW_MANAGER, kCapabilities,
                 "glasswyrmd-policy"),
      screen_(screen) {}

bool PolicyPeer::connect(std::string &error) {
  disconnect();
  if (!transport_.connect(error))
    return false;
  state_ = PeerBootstrapState::Connecting;
  return true;
}

bool PolicyPeer::send_bootstrap(std::string &error) {
  auto *connection = transport_.connection();
  gwipc_snapshot_begin begin{
      sizeof(begin), 1, GWIPC_SNAPSHOT_WINDOW_POLICY, 0, 1, 1, {}};
  gwipc_policy_context_upsert context{};
  context.struct_size = sizeof(context);
  context.root_window_id = screen_.root_window;
  context.workspace_id = 1;
  context.output_id = 1;
  context.work_width = screen_.width_pixels;
  context.work_height = screen_.height_pixels;
  gwipc_snapshot_end end{sizeof(end), 1, 1, 1, {}};
  gwipc_policy_commit commit{};
  commit.struct_size = sizeof(commit);
  commit.commit_id = 1;
  commit.producer_generation = 1;
  if (!enqueue_control(connection, GWIPC_MESSAGE_SNAPSHOT_BEGIN, begin,
                       gwipc_control_encode_snapshot_begin) ||
      !enqueue_contract(connection, GWIPC_MESSAGE_POLICY_CONTEXT_UPSERT,
                        GWIPC_FLAG_SNAPSHOT_ITEM, context,
                        gwipc_contract_encode_policy_context_upsert) ||
      !enqueue_control(connection, GWIPC_MESSAGE_SNAPSHOT_END, end,
                       gwipc_control_encode_snapshot_end) ||
      !enqueue_contract(connection, GWIPC_MESSAGE_POLICY_COMMIT,
                        GWIPC_FLAG_ACK_REQUIRED, commit,
                        gwipc_contract_encode_policy_commit)) {
    error = "could not queue policy bootstrap";
    return false;
  }
  state_ = PeerBootstrapState::AwaitingReply;
  return true;
}

bool PolicyPeer::drain(std::string &error) {
  for (;;) {
    glasswyrm::ipc::Message message;
    const auto status = transport_.handle().receive(message);
    if (status == GWIPC_STATUS_WOULD_BLOCK)
      return true;
    if (status != GWIPC_STATUS_OK) {
      error = "policy peer receive failed";
      return false;
    }
    const auto type = gwipc_message_type(message.get());
    if (type == GWIPC_MESSAGE_SNAPSHOT_BEGIN ||
        type == GWIPC_MESSAGE_SNAPSHOT_END) {
      gwipc_decoded_control *raw = nullptr;
      if (gwipc_control_decode_message(message.get(), &raw) != GWIPC_STATUS_OK)
        return false;
      std::unique_ptr<gwipc_decoded_control, DecodedControlDelete> decoded(raw);
      if (type == GWIPC_MESSAGE_SNAPSHOT_BEGIN) {
        const auto *value = gwipc_decoded_snapshot_begin(decoded.get());
        if (!value || reply_snapshot_active_ || value->snapshot_id != 1 ||
            value->domain != GWIPC_SNAPSHOT_WINDOW_POLICY ||
            value->expected_item_count != 0) {
          error = "invalid policy bootstrap snapshot";
          return false;
        }
        reply_snapshot_active_ = true;
      } else {
        const auto *value = gwipc_decoded_snapshot_end(decoded.get());
        if (!value || !reply_snapshot_active_ || value->snapshot_id != 1 ||
            value->actual_item_count != 0) {
          error = "invalid policy bootstrap snapshot end";
          return false;
        }
        reply_snapshot_active_ = false;
        reply_snapshot_complete_ = true;
      }
      continue;
    }
    if (type != GWIPC_MESSAGE_POLICY_ACKNOWLEDGED) {
      error = "unexpected policy bootstrap message";
      return false;
    }
    gwipc_decoded_contract *raw = nullptr;
    if (gwipc_contract_decode_message(message.get(), &raw) != GWIPC_STATUS_OK)
      return false;
    std::unique_ptr<gwipc_decoded_contract, DecodedContractDelete> decoded(raw);
    const auto *ack = gwipc_decoded_policy_acknowledged(decoded.get());
    if (!ack || !reply_snapshot_complete_ || ack->commit_id != 1 ||
        ack->producer_generation != 1 || ack->window_count != 0 ||
        ack->result != GWIPC_POLICY_ACCEPTED ||
        (gwipc_message_flags(message.get()) & GWIPC_FLAG_REPLY) == 0 ||
        gwipc_message_reply_to(message.get()) == 0) {
      error = "invalid policy bootstrap acknowledgement";
      return false;
    }
    policy_hash_ = ack->policy_hash;
    state_ = PeerBootstrapState::Synchronized;
  }
}

bool PolicyPeer::process(const short revents, std::string &error) {
  if (!transport_.process(revents, error)) {
    state_ = PeerBootstrapState::Failed;
    return false;
  }
  if (state_ == PeerBootstrapState::Connecting && transport_.established() &&
      !send_bootstrap(error)) {
    state_ = PeerBootstrapState::Failed;
    return false;
  }
  if (state_ == PeerBootstrapState::AwaitingReply && !drain(error)) {
    state_ = PeerBootstrapState::Failed;
    return false;
  }
  return true;
}

void PolicyPeer::disconnect() noexcept {
  transport_.disconnect();
  state_ = PeerBootstrapState::Disconnected;
  policy_hash_ = 0;
  reply_snapshot_active_ = false;
  reply_snapshot_complete_ = false;
}
} // namespace glasswyrm::server
