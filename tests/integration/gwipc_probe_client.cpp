#include <glasswyrm/ipc.h>

#include "ipc/wire/compositor_contract.hpp"
#include "ipc/wire/control.hpp"
#include "ipc/wire/envelope.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {
constexpr auto kCapabilities =
    GWIPC_CAP_FD_PASSING | GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE |
    GWIPC_CAP_SURFACE_STATE | GWIPC_CAP_MEMFD_BUFFERS |
    GWIPC_CAP_DAMAGE_REGIONS | GWIPC_CAP_SCALE_METADATA |
    GWIPC_CAP_SDR_COLOR_METADATA | GWIPC_CAP_FRAME_ACKNOWLEDGEMENT;

gwipc_status pump(gwipc_connection* connection) {
  pollfd descriptor{gwipc_connection_fd(connection),
                    gwipc_connection_wanted_poll_events(connection), 0};
  const int result = ::poll(&descriptor, 1, 100);
  if (result < 0) return errno == EINTR ? GWIPC_STATUS_OK : GWIPC_STATUS_SYSTEM_ERROR;
  return result == 0 ? GWIPC_STATUS_WOULD_BLOCK
                     : gwipc_connection_process_poll_events(connection,
                                                            descriptor.revents);
}

gwipc_status establish(const char* path, gwipc_role role,
                       std::uint64_t offered, std::uint64_t required,
                       gwipc_connection** output) {
  gwipc_connection_options options{};
  options.struct_size = sizeof(options);
  options.path = path;
  options.local_role = role;
  options.acceptable_server_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_TEST_CONSUMER);
  options.offered_capabilities = offered;
  options.required_peer_capabilities = required;
  options.instance_label = "gwipc-probe-client";
  auto status = gwipc_connection_connect(&options, output);
  if (status != GWIPC_STATUS_OK && status != GWIPC_STATUS_IN_PROGRESS)
    return status;
  for (int attempt = 0; attempt < 100; ++attempt) {
    status = pump(*output);
    if (gwipc_connection_get_state(*output) == GWIPC_CONNECTION_ESTABLISHED)
      return GWIPC_STATUS_OK;
    if (gwipc_connection_get_state(*output) == GWIPC_CONNECTION_CLOSED)
      return status;
  }
  return GWIPC_STATUS_WOULD_BLOCK;
}

gwipc_status enqueue(gwipc_connection* connection, std::uint16_t type,
                     std::uint32_t flags,
                     const std::vector<std::uint8_t>& payload,
                     const int* fds = nullptr, std::size_t fd_count = 0) {
  gwipc_outgoing_message message{};
  message.struct_size = sizeof(message);
  message.type = type;
  message.flags = flags;
  message.payload = payload.data();
  message.payload_size = payload.size();
  message.fds = fds;
  message.fd_count = fd_count;
  return gwipc_connection_enqueue(connection, &message);
}

gwipc_message* wait_message(gwipc_connection* connection,
                            std::uint16_t type) {
  for (int attempt = 0; attempt < 100; ++attempt) {
    (void)pump(connection);
    gwipc_message* message = nullptr;
    if (gwipc_connection_receive(connection, &message) == GWIPC_STATUS_OK) {
      if (gwipc_message_type(message) == type) return message;
      gwipc_message_destroy(message);
    }
  }
  return nullptr;
}

int raw_attack(const char* path, const std::string& mode) {
  const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
  if (fd < 0) return 1;
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (std::strlen(path) >= sizeof(address.sun_path)) return 1;
  std::strcpy(address.sun_path, path);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
    return 1;
  std::vector<std::uint8_t> record;
  if (mode == "malformed-envelope") {
    record.assign(40, 0);
  } else if (mode == "limit") {
    record.assign(70000, 0x5a);
  } else {
    gw::ipc::wire::Hello hello;
    hello.sender_role = gw::ipc::wire::Role::TestProducer;
    hello.offered_capabilities = kCapabilities;
    hello.maximum_payload = 65536;
    hello.maximum_fd_count = 4;
    hello.sender_instance_id[0] = 1;
    if (mode == "incompatible-version") {
      hello.minimum_major = 2;
      hello.maximum_major = 2;
    }
    const auto payload = gw::ipc::wire::encode(hello);
    gw::ipc::wire::Envelope envelope;
    envelope.type = gw::ipc::wire::MessageType::Hello;
    envelope.payload_size = payload.size();
    envelope.sequence = mode == "sequence-violation" ? 2 : 1;
    const auto header = gw::ipc::wire::encode_envelope(envelope);
    record.insert(record.end(), header.begin(), header.end());
    record.insert(record.end(), payload.begin(), payload.end());
  }
  const auto sent = ::send(fd, record.data(), record.size(), MSG_NOSIGNAL);
  if (sent < 0 && mode != "limit") return 1;
  pollfd descriptor{fd, POLLIN | POLLHUP, 0};
  if (::poll(&descriptor, 1, 1000) <= 0) {
    (void)::close(fd);
    return 1;
  }
  std::array<std::uint8_t, 1024> response{};
  const auto received = ::recv(fd, response.data(), response.size(), 0);
  bool expected_outcome = received == 0 ||
                          (received < 0 &&
                           (errno == ECONNRESET || errno == EPIPE));
  if (mode == "incompatible-version" && received > 0) {
    gw::ipc::wire::Envelope envelope;
    gw::ipc::wire::Reject rejection;
    const auto bytes = std::span(response).first(
        static_cast<std::size_t>(received));
    expected_outcome =
        gw::ipc::wire::decode_envelope(bytes, 0, 65536, envelope) ==
            gw::ipc::wire::CodecStatus::Ok &&
        envelope.type == gw::ipc::wire::MessageType::Reject &&
        gw::ipc::wire::decode(
            bytes.subspan(gw::ipc::wire::kEnvelopeSize), rejection) ==
            gw::ipc::wire::CodecStatus::Ok &&
        rejection.reason ==
            gw::ipc::wire::RejectReason::IncompatibleVersion;
  }
  (void)::close(fd);
  return expected_outcome ? 0 : 1;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 5 || std::strcmp(argv[1], "--socket") != 0 ||
      std::strcmp(argv[3], "--mode") != 0) {
    std::fprintf(stderr,
                 "usage: gwipc_probe_client --socket PATH --mode MODE\n");
    return 2;
  }
  const char* path = argv[2];
  const std::string mode = argv[4];
  if (mode == "incompatible-version" || mode == "malformed-envelope" ||
      mode == "sequence-violation" || mode == "limit")
    return raw_attack(path, mode);

  gwipc_role role = mode == "wrong-role" ? GWIPC_ROLE_DIAGNOSTIC_TOOL
                                         : GWIPC_ROLE_TEST_PRODUCER;
  std::uint64_t required = mode == "missing-capability"
                               ? GWIPC_CAP_TRACE_METADATA
                               : 0;
  gwipc_connection* raw_connection = nullptr;
  const auto status = establish(path, role, kCapabilities, required,
                                &raw_connection);
  std::unique_ptr<gwipc_connection, decltype(&gwipc_connection_destroy)>
      connection(raw_connection, gwipc_connection_destroy);
  if (mode == "wrong-role") return status == GWIPC_STATUS_ROLE_REJECTED ? 0 : 1;
  if (mode == "missing-capability")
    return status == GWIPC_STATUS_CAPABILITY_MISMATCH ? 0 : 1;
  if (status != GWIPC_STATUS_OK) return 1;

  if (mode == "roundtrip") {
    const auto payload = gw::ipc::wire::encode(gw::ipc::wire::OutputRemove{42});
    if (enqueue(connection.get(), GWIPC_MESSAGE_OUTPUT_REMOVE,
                GWIPC_FLAG_ACK_REQUIRED, payload) != GWIPC_STATUS_OK)
      return 1;
    auto* reply = wait_message(connection.get(), GWIPC_MESSAGE_OUTPUT_REMOVE);
    if (!reply) return 1;
    gwipc_message_destroy(reply);
  } else if (mode == "contract-roundtrip") {
    std::vector<std::pair<std::uint16_t, std::vector<std::uint8_t>>> contracts;
    gw::ipc::wire::OutputUpsert output;
    output.output_id = 1;
    output.enabled = true;
    output.logical_width = output.physical_pixel_width = 1920;
    output.logical_height = output.physical_pixel_height = 1080;
    output.refresh_millihertz = 60000;
    contracts.emplace_back(GWIPC_MESSAGE_OUTPUT_UPSERT,
                           gw::ipc::wire::encode(output));
    contracts.emplace_back(
        GWIPC_MESSAGE_OUTPUT_REMOVE,
        gw::ipc::wire::encode(gw::ipc::wire::OutputRemove{1}));
    gw::ipc::wire::SurfaceUpsert surface;
    surface.surface_id = 2;
    surface.logical_width = 640;
    surface.logical_height = 480;
    contracts.emplace_back(GWIPC_MESSAGE_SURFACE_UPSERT,
                           gw::ipc::wire::encode(surface));
    contracts.emplace_back(
        GWIPC_MESSAGE_SURFACE_REMOVE,
        gw::ipc::wire::encode(gw::ipc::wire::SurfaceRemove{2}));
    contracts.emplace_back(
        GWIPC_MESSAGE_BUFFER_DETACH,
        gw::ipc::wire::encode(gw::ipc::wire::BufferDetach{2, 3}));
    contracts.emplace_back(
        GWIPC_MESSAGE_BUFFER_RELEASE,
        gw::ipc::wire::encode(gw::ipc::wire::BufferRelease{
            3, gw::ipc::wire::BufferReleaseReason::ConsumerDone}));
    gw::ipc::wire::SurfaceDamage damage;
    damage.surface_id = 2;
    damage.rectangles.push_back({0, 0, 8, 8});
    contracts.emplace_back(GWIPC_MESSAGE_SURFACE_DAMAGE,
                           gw::ipc::wire::encode(damage));
    contracts.emplace_back(
        GWIPC_MESSAGE_FRAME_COMMIT,
        gw::ipc::wire::encode(gw::ipc::wire::FrameCommit{4, 1, 1, 0}));
    contracts.emplace_back(
        GWIPC_MESSAGE_FRAME_ACKNOWLEDGED,
        gw::ipc::wire::encode(gw::ipc::wire::FrameAcknowledged{
            4, 1, 1, gw::ipc::wire::FrameResult::Accepted}));
    for (const auto& [type, payload] : contracts) {
      if (enqueue(connection.get(), type, GWIPC_FLAG_ACK_REQUIRED, payload) !=
          GWIPC_STATUS_OK)
        return 1;
      auto* reply = wait_message(connection.get(), type);
      if (!reply) return 1;
      std::size_t size = 0;
      const auto* bytes = gwipc_message_payload(reply, &size);
      const bool matched = size == payload.size() &&
                           std::equal(payload.begin(), payload.end(), bytes);
      gwipc_message_destroy(reply);
      if (!matched) return 1;
    }
  } else if (mode == "ping-pong") {
    if (enqueue(connection.get(), GWIPC_MESSAGE_PING, GWIPC_FLAG_ACK_REQUIRED,
                gw::ipc::wire::encode(gw::ipc::wire::Ping{99})) !=
        GWIPC_STATUS_OK)
      return 1;
    auto* reply = wait_message(connection.get(), GWIPC_MESSAGE_PONG);
    if (!reply) return 1;
    gwipc_message_destroy(reply);
  } else if (mode == "snapshot") {
    if (enqueue(connection.get(), GWIPC_MESSAGE_SNAPSHOT_BEGIN, 0,
                gw::ipc::wire::encode(gw::ipc::wire::SnapshotBegin{
                    1, gw::ipc::wire::SnapshotDomain::Test, 0, 1, 1})) !=
            GWIPC_STATUS_OK ||
        enqueue(connection.get(), GWIPC_MESSAGE_OUTPUT_REMOVE,
                GWIPC_FLAG_SNAPSHOT_ITEM,
                gw::ipc::wire::encode(gw::ipc::wire::OutputRemove{1})) !=
            GWIPC_STATUS_OK ||
        enqueue(connection.get(), GWIPC_MESSAGE_SNAPSHOT_END, 0,
                gw::ipc::wire::encode(
                    gw::ipc::wire::SnapshotEnd{1, 1, 1})) != GWIPC_STATUS_OK)
      return 1;
    if (enqueue(connection.get(), GWIPC_MESSAGE_PING, GWIPC_FLAG_ACK_REQUIRED,
                gw::ipc::wire::encode(gw::ipc::wire::Ping{101})) !=
        GWIPC_STATUS_OK)
      return 1;
    auto* confirmation = wait_message(connection.get(), GWIPC_MESSAGE_PONG);
    if (!confirmation) return 1;
    gwipc_message_destroy(confirmation);
  } else if (mode == "fd-transfer") {
    for (int repetition = 0; repetition < 16; ++repetition) {
      const int fd =
          ::memfd_create("gwipc-probe", MFD_CLOEXEC | MFD_ALLOW_SEALING);
      std::array<std::uint8_t, 16> content{};
      for (std::size_t byte = 0; byte < content.size(); ++byte)
        content[byte] = static_cast<std::uint8_t>(byte);
      if (fd < 0 || ::ftruncate(fd, 4096) < 0 ||
          ::pwrite(fd, content.data(), content.size(), 0) !=
              static_cast<ssize_t>(content.size()) ||
          ::fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW) < 0)
        return 1;
      gw::ipc::wire::BufferAttach attach;
      attach.buffer_id = 3;
      attach.surface_id = 4;
      attach.width = 8;
      attach.height = 8;
      attach.stride = 32;
      attach.storage_size = 4096;
      const auto queued = enqueue(connection.get(), GWIPC_MESSAGE_BUFFER_ATTACH,
                                  GWIPC_FLAG_ACK_REQUIRED,
                                  gw::ipc::wire::encode(attach), &fd, 1);
      (void)::close(fd);
      if (queued != GWIPC_STATUS_OK) return 1;
      auto* reply =
          wait_message(connection.get(), GWIPC_MESSAGE_BUFFER_RELEASE);
      if (!reply) return 1;
      gwipc_message_destroy(reply);
    }
  } else {
    return 2;
  }
  std::printf("gwipc probe client: mode=%s passed\n", mode.c_str());
  return 0;
}
