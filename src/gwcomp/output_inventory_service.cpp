#include "gwcomp/output_inventory_service.hpp"

#include "gwcomp/output_inventory_publisher.hpp"

#include <memory>
#include <string>
#include <utility>

namespace glasswyrm::compositor {
namespace {

constexpr std::uint64_t kOutputModelCapabilities =
    GWIPC_CAP_OUTPUT_MANAGEMENT | GWIPC_CAP_SURFACE_OUTPUT_MEMBERSHIP |
    GWIPC_CAP_SCALE_METADATA;
constexpr std::uint64_t kVrrCapabilities =
    GWIPC_CAP_VRR_METADATA | GWIPC_CAP_VRR_POLICY |
    GWIPC_CAP_PRESENTATION_TIMING;

struct DecodedContractDeleter {
  void operator()(gwipc_decoded_contract* value) const {
    gwipc_decoded_contract_destroy(value);
  }
};

OutputInventoryServiceResult reject_peer(std::string reason,
                                         const gwipc_status status) {
  return {OutputInventoryDisposition::RejectPeer, status, std::move(reason)};
}

OutputInventoryServiceResult fatal(const char* reason,
                                   const gwipc_status status) {
  return {OutputInventoryDisposition::Fatal, status, reason};
}

}  // namespace

std::uint64_t OutputInventoryService::allocate_snapshot_id() noexcept {
  if (next_snapshot_id_ == 0) return 0;
  const auto snapshot_id = next_snapshot_id_;
  next_snapshot_id_ = snapshot_id == UINT64_MAX ? 0 : snapshot_id + 1U;
  return snapshot_id;
}

OutputInventoryServiceResult OutputInventoryService::service(
    gwipc_connection& connection, const gwipc_role peer_role,
    const gwipc_message& message) {
  if (gwipc_message_type(&message) != GWIPC_MESSAGE_OUTPUT_STATE_QUERY)
    return {};
  if (peer_role != GWIPC_ROLE_PROTOCOL_SERVER ||
      (gwipc_connection_peer_info(&connection).capabilities &
       kOutputModelCapabilities) != kOutputModelCapabilities)
    return reject_peer("invalid protocol-server query",
                       GWIPC_STATUS_PROTOCOL_ERROR);

  gwipc_decoded_contract* raw_contract = nullptr;
  auto status = gwipc_contract_decode_message(&message, &raw_contract);
  std::unique_ptr<gwipc_decoded_contract, DecodedContractDeleter> contract(
      raw_contract);
  const auto* query =
      status == GWIPC_STATUS_OK
          ? gwipc_decoded_output_state_query(contract.get())
          : nullptr;
  if (query == nullptr)
    return reject_peer("invalid OutputStateQuery payload",
                       status == GWIPC_STATUS_OK ? GWIPC_STATUS_PROTOCOL_ERROR
                                                : status);

  std::optional<gw::compositor::VrrInventorySnapshot> vrr_snapshot;
  std::optional<OutputInventoryVrr> vrr;
  if ((query->flags & GWIPC_OUTPUT_QUERY_VRR) != 0) {
    if ((gwipc_connection_peer_info(&connection).capabilities &
         kVrrCapabilities) != kVrrCapabilities)
      return reject_peer("VRR inventory query was not negotiated",
                         GWIPC_STATUS_PROTOCOL_ERROR);
    if (!compositor_)
      return fatal("VRR inventory provider is unavailable",
                   GWIPC_STATUS_INVALID_STATE);
    std::string error;
    vrr_snapshot = compositor_->vrr_inventory(layout_, error);
    if (!vrr_snapshot)
      return fatal(error.c_str(), GWIPC_STATUS_INVALID_STATE);
    vrr.emplace(OutputInventoryVrr{
        vrr_snapshot->capabilities, vrr_snapshot->policies,
        vrr_snapshot->states, vrr_snapshot->timings, {}});
  }

  const auto snapshot_id = allocate_snapshot_id();
  if (snapshot_id == 0)
    return fatal("snapshot identifier space exhausted",
                 GWIPC_STATUS_LIMIT_EXCEEDED);
  const auto publication = build_output_inventory_publication(
      *query, gwipc_message_sequence(&message), snapshot_id, layout_, {},
      vrr ? &*vrr : nullptr);
  if (!publication)
    return fatal("could not build an atomic publication", publication.status);

  for (std::size_t index = 0; index < publication.messages.size(); ++index) {
    const auto& record = publication.messages[index];
    gwipc_outgoing_message outgoing{};
    outgoing.struct_size = sizeof(outgoing);
    outgoing.type = record.type;
    outgoing.flags = record.flags;
    outgoing.reply_to = record.reply_to;
    outgoing.payload = record.payload.data();
    outgoing.payload_size = record.payload.size();
    status = gwipc_connection_enqueue(&connection, &outgoing);
    if (status != GWIPC_STATUS_OK)
      return reject_peer(
          "partial output inventory publication was discarded at record " +
              std::to_string(index + 1U) + " of " +
              std::to_string(publication.messages.size()),
          status);
  }
  return {OutputInventoryDisposition::Handled, GWIPC_STATUS_OK, {}};
}

}  // namespace glasswyrm::compositor
