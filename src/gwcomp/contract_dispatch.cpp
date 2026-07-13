#include "gwcomp/contract_dispatch.hpp"

#include <cstdio>
#include <memory>
#include <string>

namespace glasswyrm::compositor {
namespace {

struct ContractDeleter {
  void operator()(gwipc_decoded_contract* value) const {
    gwipc_decoded_contract_destroy(value);
  }
};

struct PayloadDeleter {
  void operator()(gwipc_contract_payload* value) const {
    gwipc_contract_payload_destroy(value);
  }
};

bool is_complete_session_snapshot(const gwipc_message* message) {
  gwipc_decoded_control* raw = nullptr;
  if (gwipc_control_decode_message(message, &raw) != GWIPC_STATUS_OK)
    return false;
  std::unique_ptr<gwipc_decoded_control, decltype(&gwipc_decoded_control_destroy)>
      control(raw, gwipc_decoded_control_destroy);
  const auto* begin = gwipc_decoded_snapshot_begin(control.get());
  return begin && begin->domain == GWIPC_SNAPSHOT_COMPLETE_SESSION;
}

bool enqueue_ack(gwipc_connection* connection, const gwipc_message* message,
                 const gwipc_frame_commit& commit,
                 const gw::compositor::PresentedFrame& frame) {
  gwipc_frame_acknowledged acknowledged{};
  acknowledged.struct_size = sizeof(acknowledged);
  acknowledged.commit_id = commit.commit_id;
  acknowledged.output_id = commit.output_id;
  acknowledged.presented_generation = frame.generation;
  acknowledged.result = frame.result;
  gwipc_contract_payload* raw_payload = nullptr;
  if (gwipc_contract_encode_frame_acknowledged(&acknowledged, &raw_payload) !=
      GWIPC_STATUS_OK)
    return false;
  std::unique_ptr<gwipc_contract_payload, PayloadDeleter> payload(raw_payload);
  std::size_t size = 0;
  const auto* bytes = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message outgoing{};
  outgoing.struct_size = sizeof(outgoing);
  outgoing.type = GWIPC_MESSAGE_FRAME_ACKNOWLEDGED;
  outgoing.flags = GWIPC_FLAG_REPLY;
  outgoing.reply_to = gwipc_message_sequence(message);
  outgoing.payload = bytes;
  outgoing.payload_size = size;
  const auto status = gwipc_connection_enqueue(connection, &outgoing);
  if (status != GWIPC_STATUS_OK) {
    std::fprintf(stderr, "gwcomp: acknowledgement enqueue failed: %s\n",
                 gwipc_status_string(status));
  }
  return status == GWIPC_STATUS_OK;
}

bool enqueue_release(gwipc_connection* connection, std::uint64_t buffer_id,
                     gwipc_buffer_release_reason reason) {
  gwipc_buffer_release release{};
  release.struct_size = sizeof(release);
  release.buffer_id = buffer_id;
  release.reason = reason;
  gwipc_contract_payload* raw_payload = nullptr;
  if (gwipc_contract_encode_buffer_release(&release, &raw_payload) !=
      GWIPC_STATUS_OK)
    return false;
  std::unique_ptr<gwipc_contract_payload, PayloadDeleter> payload(raw_payload);
  std::size_t size = 0;
  const auto* bytes = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message outgoing{};
  outgoing.struct_size = sizeof(outgoing);
  outgoing.type = GWIPC_MESSAGE_BUFFER_RELEASE;
  outgoing.payload = bytes;
  outgoing.payload_size = size;
  return gwipc_connection_enqueue(connection, &outgoing) == GWIPC_STATUS_OK;
}

void enqueue_releases(gwipc_connection* connection,
                      gw::compositor::Compositor& compositor) {
  for (const auto& [buffer_id, reason] : compositor.releases())
    (void)enqueue_release(connection, buffer_id, reason);
  compositor.clear_releases();
}

} // namespace

ContractDispatchResult dispatch_contract_message(
    gwipc_connection* connection, gwipc_message* message,
    const gwipc_role peer_role,
    const gw::compositor::PeerProfile peer_profile,
    const std::optional<std::uint64_t> maximum_frames,
    gw::compositor::Compositor& compositor) {
  ContractDispatchResult result;
  const auto type = gwipc_message_type(message);
  if (type == GWIPC_MESSAGE_SNAPSHOT_BEGIN) {
    if (!is_complete_session_snapshot(message) ||
        !compositor.begin_snapshot()) {
      std::fprintf(stderr, "gwcomp: rejected invalid snapshot begin\n");
    }
    return result;
  }
  if (type == GWIPC_MESSAGE_SNAPSHOT_END) {
    if (!compositor.end_snapshot())
      std::fprintf(stderr, "gwcomp: rejected invalid snapshot end\n");
    return result;
  }
  if (type == GWIPC_MESSAGE_SNAPSHOT_ABORT) {
    compositor.abort_snapshot();
    return result;
  }

  gwipc_decoded_contract* raw_contract = nullptr;
  if (gwipc_contract_decode_message(message, &raw_contract) !=
      GWIPC_STATUS_OK) {
    std::fprintf(stderr,
                 "gwcomp: rejected undecodable contract type=0x%04x\n", type);
    return result;
  }
  std::unique_ptr<gwipc_decoded_contract, ContractDeleter> contract(
      raw_contract);
  bool applied = true;
  switch (type) {
    case GWIPC_MESSAGE_OUTPUT_UPSERT:
      applied = compositor.apply(*gwipc_decoded_output_upsert(contract.get()));
      break;
    case GWIPC_MESSAGE_OUTPUT_REMOVE:
      applied = compositor.apply(*gwipc_decoded_output_remove(contract.get()));
      break;
    case GWIPC_MESSAGE_SURFACE_UPSERT:
      applied = compositor.apply(*gwipc_decoded_surface_upsert(contract.get()));
      break;
    case GWIPC_MESSAGE_SURFACE_POLICY_UPSERT:
      applied = peer_role == GWIPC_ROLE_PROTOCOL_SERVER &&
                compositor.apply(
                    *gwipc_decoded_surface_policy_upsert(contract.get()));
      break;
    case GWIPC_MESSAGE_SURFACE_REMOVE:
      applied = compositor.apply(*gwipc_decoded_surface_remove(contract.get()));
      break;
    case GWIPC_MESSAGE_SURFACE_DAMAGE:
      applied = peer_profile !=
                    gw::compositor::PeerProfile::M6MetadataProtocolServer &&
                compositor.apply(
                    *gwipc_decoded_surface_damage(contract.get()));
      break;
    case GWIPC_MESSAGE_BUFFER_ATTACH: {
      int fd = -1;
      std::string error;
      applied = peer_profile !=
                    gw::compositor::PeerProfile::M6MetadataProtocolServer &&
                gwipc_message_take_fd(message, 0, &fd) == GWIPC_STATUS_OK &&
                compositor.attach(
                    *gwipc_decoded_buffer_attach(contract.get()), fd, error);
      if (!applied)
        std::fprintf(stderr, "gwcomp: buffer rejected: %s\n", error.c_str());
      break;
    }
    case GWIPC_MESSAGE_BUFFER_DETACH:
      applied = peer_profile !=
                    gw::compositor::PeerProfile::M6MetadataProtocolServer &&
                compositor.detach(
                    *gwipc_decoded_buffer_detach(contract.get()));
      break;
    case GWIPC_MESSAGE_FRAME_COMMIT: {
      const auto& commit = *gwipc_decoded_frame_commit(contract.get());
      std::string error;
      auto frame = compositor.commit(commit, error);
      if ((gwipc_message_flags(message) & GWIPC_FLAG_ACK_REQUIRED) == 0)
        frame.result = GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
      (void)enqueue_ack(connection, message, commit, frame);
      if (frame.result == GWIPC_FRAME_ACCEPTED) {
        result.accepted_frame = true;
        std::fprintf(
            stderr,
            "gwcomp: frame accepted commit=%llu frame=%llu hash=%016llx\n",
            static_cast<unsigned long long>(commit.commit_id),
            static_cast<unsigned long long>(frame.ordinal),
            static_cast<unsigned long long>(frame.hash));
        if (maximum_frames && compositor.accepted_frames() == *maximum_frames)
          result.stop_after_flush = true;
      } else {
        std::fprintf(stderr,
                     "gwcomp: frame rejected result=%u reason=%s\n",
                     static_cast<unsigned>(frame.result), error.c_str());
      }
      break;
    }
    default:
      applied = false;
      break;
  }
  if (!applied && type != GWIPC_MESSAGE_FRAME_COMMIT)
    std::fprintf(stderr, "gwcomp: rejected contract type=0x%04x\n", type);
  enqueue_releases(connection, compositor);
  return result;
}

} // namespace glasswyrm::compositor
