#include "glasswyrmd/compositor_peer.hpp"

#include <memory>

namespace glasswyrm::server {
namespace {
constexpr gwipc_capabilities kCapabilities =
    GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE | GWIPC_CAP_SURFACE_STATE |
    GWIPC_CAP_SDR_COLOR_METADATA | GWIPC_CAP_FRAME_ACKNOWLEDGEMENT |
    GWIPC_CAP_WINDOW_LIFECYCLE;
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
struct DecodedDelete {
  void operator()(gwipc_decoded_contract *p) const {
    gwipc_decoded_contract_destroy(p);
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

CompositorPeer::CompositorPeer(std::string path,
                               const gw::protocol::x11::ScreenModel screen)
    : transport_(std::move(path), GWIPC_ROLE_COMPOSITOR, kCapabilities,
                 "glasswyrmd-compositor"),
      screen_(screen) {}

bool CompositorPeer::connect(std::string &error) {
  disconnect();
  if (!transport_.connect(error))
    return false;
  state_ = PeerBootstrapState::Connecting;
  return true;
}

bool CompositorPeer::send_bootstrap(std::string &error) {
  auto *connection = transport_.connection();
  gwipc_snapshot_begin begin{
      sizeof(begin), 1, GWIPC_SNAPSHOT_COMPLETE_SESSION, 0, 1, 1, {}};
  gwipc_output_upsert output{};
  output.struct_size = sizeof(output);
  output.output_id = 1;
  output.enabled = 1;
  output.logical_width = screen_.width_pixels;
  output.logical_height = screen_.height_pixels;
  output.physical_pixel_width = screen_.width_pixels;
  output.physical_pixel_height = screen_.height_pixels;
  output.refresh_millihertz = 60000;
  output.scale_numerator = 1;
  output.scale_denominator = 1;
  output.transform = GWIPC_TRANSFORM_NORMAL;
  output.color.color_space = GWIPC_SDR_COLOR_SPACE_SRGB;
  output.color.transfer_function = GWIPC_TRANSFER_FUNCTION_SRGB;
  output.color.primaries = GWIPC_COLOR_PRIMARIES_SRGB;
  gwipc_snapshot_end end{sizeof(end), 1, 1, 1, {}};
  gwipc_frame_commit commit{};
  commit.struct_size = sizeof(commit);
  commit.commit_id = 1;
  commit.output_id = 1;
  commit.producer_generation = 1;
  if (!enqueue_control(connection, GWIPC_MESSAGE_SNAPSHOT_BEGIN, begin,
                       gwipc_control_encode_snapshot_begin) ||
      !enqueue_contract(connection, GWIPC_MESSAGE_OUTPUT_UPSERT,
                        GWIPC_FLAG_SNAPSHOT_ITEM, output,
                        gwipc_contract_encode_output_upsert) ||
      !enqueue_control(connection, GWIPC_MESSAGE_SNAPSHOT_END, end,
                       gwipc_control_encode_snapshot_end) ||
      !enqueue_contract(connection, GWIPC_MESSAGE_FRAME_COMMIT,
                        GWIPC_FLAG_ACK_REQUIRED, commit,
                        gwipc_contract_encode_frame_commit)) {
    error = "could not queue compositor bootstrap";
    return false;
  }
  state_ = PeerBootstrapState::AwaitingReply;
  return true;
}

bool CompositorPeer::drain(std::string &error) {
  for (;;) {
    glasswyrm::ipc::Message message;
    const auto status = transport_.handle().receive(message);
    if (status == GWIPC_STATUS_WOULD_BLOCK)
      return true;
    if (status != GWIPC_STATUS_OK) {
      error = "compositor peer receive failed";
      return false;
    }
    if (gwipc_message_type(message.get()) != GWIPC_MESSAGE_FRAME_ACKNOWLEDGED) {
      error = gwipc_message_type(message.get()) == GWIPC_MESSAGE_BUFFER_RELEASE
                  ? "metadata-only compositor sent a buffer release"
                  : "unexpected compositor bootstrap message";
      return false;
    }
    gwipc_decoded_contract *raw = nullptr;
    if (gwipc_contract_decode_message(message.get(), &raw) != GWIPC_STATUS_OK)
      return false;
    std::unique_ptr<gwipc_decoded_contract, DecodedDelete> decoded(raw);
    const auto *ack = gwipc_decoded_frame_acknowledged(decoded.get());
    if (!ack || ack->commit_id != 1 || ack->output_id != 1 ||
        ack->presented_generation != 1 || ack->result != GWIPC_FRAME_ACCEPTED ||
        (gwipc_message_flags(message.get()) & GWIPC_FLAG_REPLY) == 0 ||
        gwipc_message_reply_to(message.get()) == 0) {
      error = "invalid compositor bootstrap acknowledgement";
      return false;
    }
    state_ = PeerBootstrapState::Synchronized;
  }
}

bool CompositorPeer::process(const short revents, std::string &error) {
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

void CompositorPeer::disconnect() noexcept {
  transport_.disconnect();
  state_ = PeerBootstrapState::Disconnected;
}
} // namespace glasswyrm::server
