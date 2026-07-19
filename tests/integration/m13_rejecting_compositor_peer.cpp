#include "backends/headless/inventory.hpp"
#include "gwcomp/output_inventory_service.hpp"

#include <glasswyrm/ipc.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <memory>
#include <poll.h>
#include <string>
#include <vector>

namespace {

volatile sig_atomic_t running = 1;
void stop(int) { running = 0; }

constexpr std::uint64_t kCommonCapabilities =
    GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE | GWIPC_CAP_SURFACE_STATE |
    GWIPC_CAP_SDR_COLOR_METADATA | GWIPC_CAP_FRAME_ACKNOWLEDGEMENT;
constexpr std::uint64_t kOfferedCapabilities =
    kCommonCapabilities | GWIPC_CAP_FD_PASSING | GWIPC_CAP_MEMFD_BUFFERS |
    GWIPC_CAP_DAMAGE_REGIONS | GWIPC_CAP_WINDOW_LIFECYCLE |
    GWIPC_CAP_SESSION_STATE | GWIPC_CAP_CURSOR_SURFACE |
    GWIPC_CAP_CPU_BUFFER_SYNCHRONIZATION | GWIPC_CAP_OUTPUT_MANAGEMENT |
    GWIPC_CAP_SURFACE_OUTPUT_MEMBERSHIP | GWIPC_CAP_SCALE_METADATA;

struct PayloadDelete {
  void operator()(gwipc_contract_payload* value) const noexcept {
    gwipc_contract_payload_destroy(value);
  }
};

bool acknowledge_frame(gwipc_connection* connection,
                       const gwipc_message* message,
                       const bool accept) {
  gwipc_decoded_contract* raw = nullptr;
  if (gwipc_contract_decode_message(message, &raw) != GWIPC_STATUS_OK)
    return false;
  std::unique_ptr<gwipc_decoded_contract,
                  decltype(&gwipc_decoded_contract_destroy)>
      decoded(raw, gwipc_decoded_contract_destroy);
  const auto* commit = gwipc_decoded_frame_commit(decoded.get());
  if (!commit)
    return false;

  gwipc_frame_acknowledged acknowledged{};
  acknowledged.struct_size = sizeof(acknowledged);
  acknowledged.commit_id = commit->commit_id;
  acknowledged.output_id = commit->output_id;
  acknowledged.presented_generation = commit->producer_generation;
  acknowledged.result = accept ? GWIPC_FRAME_ACCEPTED
                               : GWIPC_FRAME_REJECTED_INCOMPLETE_METADATA;
  gwipc_contract_payload* raw_payload = nullptr;
  if (gwipc_contract_encode_frame_acknowledged(&acknowledged, &raw_payload) !=
      GWIPC_STATUS_OK)
    return false;
  std::unique_ptr<gwipc_contract_payload, PayloadDelete> payload(raw_payload);
  std::size_t size = 0;
  const auto* data = gwipc_contract_payload_data(payload.get(), &size);
  gwipc_outgoing_message outgoing{};
  outgoing.struct_size = sizeof(outgoing);
  outgoing.type = GWIPC_MESSAGE_FRAME_ACKNOWLEDGED;
  outgoing.flags = GWIPC_FLAG_REPLY;
  outgoing.reply_to = gwipc_message_sequence(message);
  outgoing.payload = data;
  outgoing.payload_size = size;
  return gwipc_connection_enqueue(connection, &outgoing) == GWIPC_STATUS_OK;
}

bool service_messages(
    gwipc_connection* connection,
    glasswyrm::compositor::OutputInventoryService& inventory,
    std::uint64_t& frame_count) {
  while (true) {
    gwipc_message* raw = nullptr;
    const auto status = gwipc_connection_receive(connection, &raw);
    if (status == GWIPC_STATUS_WOULD_BLOCK)
      return true;
    if (status != GWIPC_STATUS_OK)
      return false;
    std::unique_ptr<gwipc_message, decltype(&gwipc_message_destroy)> message(
        raw, gwipc_message_destroy);
    const auto result = inventory.service(
        *connection, gwipc_connection_peer_info(connection).role, *message);
    if (result.disposition ==
        glasswyrm::compositor::OutputInventoryDisposition::Handled)
      continue;
    if (result.disposition !=
        glasswyrm::compositor::OutputInventoryDisposition::NotHandled)
      return false;
    if (gwipc_message_type(message.get()) != GWIPC_MESSAGE_FRAME_COMMIT)
      continue;
    ++frame_count;
    if (!acknowledge_frame(connection, message.get(), frame_count != 2))
      return false;
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: m13_rejecting_compositor_peer SOCKET\n");
    return 2;
  }
  (void)::signal(SIGTERM, stop);
  (void)::signal(SIGINT, stop);

  const std::vector<glasswyrm::headless::OutputRequest> outputs{
      {"LEFT", 640, 480, 60'000}, {"RIGHT", 640, 480, 60'000}};
  std::string error;
  const auto model =
      glasswyrm::headless::OutputInventory::build(outputs, error);
  if (!model) {
    std::fprintf(stderr, "rejecting peer inventory failed: %s\n",
                 error.c_str());
    return 1;
  }
  glasswyrm::compositor::OutputInventoryService inventory(model->layout());

  gwipc_listener_options options{};
  options.struct_size = sizeof(options);
  options.path = argv[1];
  options.local_role = GWIPC_ROLE_COMPOSITOR;
  options.accepted_peer_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_PROTOCOL_SERVER);
  options.offered_capabilities = kOfferedCapabilities;
  options.required_peer_capabilities = kCommonCapabilities;
  options.maximum_payload = GWIPC_DEFAULT_MAXIMUM_PAYLOAD;
  options.maximum_fd_count = GWIPC_DEFAULT_MAXIMUM_FDS;
  options.maximum_queued_bytes = GWIPC_DEFAULT_MAXIMUM_QUEUED_BYTES;
  options.maximum_queued_messages = GWIPC_DEFAULT_MAXIMUM_QUEUED_MESSAGES;
  options.instance_label = "m13-rejecting-compositor-peer";
  gwipc_listener* raw_listener = nullptr;
  if (gwipc_listener_create(&options, &raw_listener) != GWIPC_STATUS_OK)
    return 1;
  std::unique_ptr<gwipc_listener, decltype(&gwipc_listener_destroy)> listener(
      raw_listener, gwipc_listener_destroy);
  std::unique_ptr<gwipc_connection, decltype(&gwipc_connection_destroy)>
      connection(nullptr, gwipc_connection_destroy);
  std::uint64_t frame_count = 0;

  while (running) {
    pollfd descriptors[2]{
        {gwipc_listener_fd(listener.get()),
         static_cast<short>(connection ? 0 : POLLIN), 0},
        {connection ? gwipc_connection_fd(connection.get()) : -1,
         static_cast<short>(
             connection
                 ? gwipc_connection_wanted_poll_events(connection.get())
                 : 0),
         0}};
    const auto count = ::poll(descriptors, 2, 100);
    if (count < 0) {
      if (errno == EINTR)
        continue;
      return 1;
    }
    if (!connection && (descriptors[0].revents & POLLIN) != 0) {
      gwipc_connection* accepted = nullptr;
      if (gwipc_listener_accept(listener.get(), &accepted) == GWIPC_STATUS_OK)
        connection.reset(accepted);
    }
    if (!connection)
      continue;
    if (descriptors[1].revents != 0 &&
        gwipc_connection_process_poll_events(connection.get(),
                                             descriptors[1].revents) !=
            GWIPC_STATUS_OK)
      return 1;
    if (!service_messages(connection.get(), inventory, frame_count))
      return 1;
    if (gwipc_connection_get_state(connection.get()) ==
        GWIPC_CONNECTION_CLOSED)
      return running ? 1 : 0;
  }
  return 0;
}
