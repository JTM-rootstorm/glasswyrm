#include <glasswyrm/ipc.h>

#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint64_t kCapabilities =
    GWIPC_CAP_FD_PASSING | GWIPC_CAP_SNAPSHOTS | GWIPC_CAP_OUTPUT_STATE |
    GWIPC_CAP_SURFACE_STATE | GWIPC_CAP_MEMFD_BUFFERS |
    GWIPC_CAP_DAMAGE_REGIONS | GWIPC_CAP_SDR_COLOR_METADATA |
    GWIPC_CAP_FRAME_ACKNOWLEDGEMENT;
constexpr std::array<std::string_view, 12> kScenarios{
    "basic",          "damage-update", "stacking",       "visibility",
    "clipping",       "opacity",       "buffer-replace", "detach-remove",
    "invalid-metadata", "invalid-buffer", "snapshot-reconnect",
    "unknown-reference"};

struct ConnectionDeleter {
  void operator()(gwipc_connection* value) const {
    gwipc_connection_destroy(value);
  }
};
struct MessageDeleter {
  void operator()(gwipc_message* value) const { gwipc_message_destroy(value); }
};
struct PayloadDeleter {
  void operator()(gwipc_contract_payload* value) const {
    gwipc_contract_payload_destroy(value);
  }
};
using Connection = std::unique_ptr<gwipc_connection, ConnectionDeleter>;
using Message = std::unique_ptr<gwipc_message, MessageDeleter>;
using Payload = std::unique_ptr<gwipc_contract_payload, PayloadDeleter>;

void usage(FILE* stream) {
  std::fprintf(stream,
               "Usage: gwcomp_m4_producer --socket PATH --scenario NAME\n"
               "Scenarios: basic, damage-update, stacking, visibility, "
               "clipping, opacity, buffer-replace, detach-remove, "
               "invalid-metadata, invalid-buffer, snapshot-reconnect, "
               "unknown-reference\n");
}

void put_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}
void put_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8)
    bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}
void put_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
  for (unsigned shift = 0; shift < 64; shift += 8)
    bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

bool enqueue(gwipc_connection* connection, std::uint16_t type,
             std::uint32_t flags, const std::uint8_t* bytes, std::size_t size,
             const int* fds = nullptr, std::size_t fd_count = 0) {
  gwipc_outgoing_message outgoing{};
  outgoing.struct_size = sizeof(outgoing);
  outgoing.type = type;
  outgoing.flags = flags;
  outgoing.payload = bytes;
  outgoing.payload_size = size;
  outgoing.fds = fds;
  outgoing.fd_count = fd_count;
  return gwipc_connection_enqueue(connection, &outgoing) == GWIPC_STATUS_OK;
}

template <class Value, class Encoder>
bool enqueue_contract(gwipc_connection* connection, std::uint16_t type,
                      std::uint32_t flags, const Value& value, Encoder encoder,
                      const int* fds = nullptr, std::size_t fd_count = 0) {
  gwipc_contract_payload* raw = nullptr;
  if (encoder(&value, &raw) != GWIPC_STATUS_OK) return false;
  const Payload payload(raw);
  std::size_t size = 0;
  const auto* bytes = gwipc_contract_payload_data(payload.get(), &size);
  return enqueue(connection, type, flags, bytes, size, fds, fd_count);
}

bool pump(gwipc_connection* connection, int timeout_ms) {
  pollfd descriptor{gwipc_connection_fd(connection),
                    gwipc_connection_wanted_poll_events(connection), 0};
  const int ready = ::poll(&descriptor, 1, timeout_ms);
  if (ready < 0 && errno == EINTR) return true;
  if (ready < 0) return false;
  if (ready > 0 && gwipc_connection_process_poll_events(
                       connection, descriptor.revents) == GWIPC_STATUS_SYSTEM_ERROR)
    return false;
  return gwipc_connection_get_state(connection) != GWIPC_CONNECTION_CLOSED;
}

int make_buffer(std::uint32_t width, std::uint32_t height,
                std::uint32_t pixel) {
  const std::size_t size = static_cast<std::size_t>(width) * height * 4U;
  const int fd = ::memfd_create("gwcomp-m4-producer", MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd < 0 || ::ftruncate(fd, static_cast<off_t>(size)) != 0) {
    if (fd >= 0) (void)::close(fd);
    return -1;
  }
  void* mapping = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (mapping == MAP_FAILED) {
    (void)::close(fd);
    return -1;
  }
  auto* pixels = static_cast<std::uint32_t*>(mapping);
  for (std::size_t index = 0; index < size / 4U; ++index) pixels[index] = pixel;
  const bool ok = ::msync(mapping, size, MS_SYNC) == 0 &&
                  ::munmap(mapping, size) == 0 &&
                  ::fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW) == 0;
  if (!ok) {
    (void)::close(fd);
    return -1;
  }
  return fd;
}

gwipc_sdr_color_metadata srgb() {
  return {GWIPC_SDR_COLOR_SPACE_SRGB, GWIPC_TRANSFER_FUNCTION_SRGB,
          GWIPC_COLOR_PRIMARIES_SRGB, 0, 0, 0, 0};
}

bool send_basic(gwipc_connection* connection) {
  std::vector<std::uint8_t> begin;
  put_u64(begin, 1); put_u16(begin, 4); put_u16(begin, 0);
  put_u64(begin, 1); put_u32(begin, 5); put_u32(begin, 0);
  if (!enqueue(connection, GWIPC_MESSAGE_SNAPSHOT_BEGIN, 0, begin.data(), begin.size()))
    return false;

  gwipc_output_upsert output{};
  output.struct_size = sizeof(output); output.output_id = 1; output.enabled = 1;
  output.logical_width = output.physical_pixel_width = 320;
  output.logical_height = output.physical_pixel_height = 200;
  output.refresh_millihertz = 60'000; output.scale_numerator = 1;
  output.scale_denominator = 1; output.transform = GWIPC_TRANSFORM_NORMAL;
  output.color = srgb();
  if (!enqueue_contract(connection, GWIPC_MESSAGE_OUTPUT_UPSERT,
                        GWIPC_FLAG_SNAPSHOT_ITEM, output,
                        gwipc_contract_encode_output_upsert)) return false;

  gwipc_surface_upsert background{};
  background.struct_size = sizeof(background); background.surface_id = 1;
  background.output_id = 1; background.logical_width = 320;
  background.logical_height = 200; background.visible = 1;
  background.transform = GWIPC_TRANSFORM_NORMAL;
  background.opacity = GWIPC_OPACITY_ONE; background.scale_numerator = 1;
  background.scale_denominator = 1; background.color = srgb();
  gwipc_surface_upsert overlay = background;
  overlay.surface_id = 2; overlay.logical_x = 80; overlay.logical_y = 50;
  overlay.logical_width = 80; overlay.logical_height = 60; overlay.stacking = 1;
  if (!enqueue_contract(connection, GWIPC_MESSAGE_SURFACE_UPSERT,
                        GWIPC_FLAG_SNAPSHOT_ITEM, background,
                        gwipc_contract_encode_surface_upsert) ||
      !enqueue_contract(connection, GWIPC_MESSAGE_SURFACE_UPSERT,
                        GWIPC_FLAG_SNAPSHOT_ITEM, overlay,
                        gwipc_contract_encode_surface_upsert)) return false;

  const int background_fd = make_buffer(320, 200, UINT32_C(0xff204060));
  const int overlay_fd = make_buffer(80, 60, UINT32_C(0x80800000));
  if (background_fd < 0 || overlay_fd < 0) {
    if (background_fd >= 0) (void)::close(background_fd);
    if (overlay_fd >= 0) (void)::close(overlay_fd);
    return false;
  }
  gwipc_buffer_attach attachment{};
  attachment.struct_size = sizeof(attachment); attachment.buffer_id = 1;
  attachment.surface_id = 1; attachment.width = 320; attachment.height = 200;
  attachment.stride = 1280; attachment.storage_size = 320U * 200U * 4U;
  attachment.pixel_format = GWIPC_PIXEL_FORMAT_XRGB8888;
  attachment.alpha_semantics = GWIPC_ALPHA_OPAQUE; attachment.color = srgb();
  const bool first = enqueue_contract(connection, GWIPC_MESSAGE_BUFFER_ATTACH,
      GWIPC_FLAG_SNAPSHOT_ITEM, attachment, gwipc_contract_encode_buffer_attach,
      &background_fd, 1);
  attachment.buffer_id = 2; attachment.surface_id = 2; attachment.width = 80;
  attachment.height = 60; attachment.stride = 320;
  attachment.storage_size = 80U * 60U * 4U;
  attachment.pixel_format = GWIPC_PIXEL_FORMAT_ARGB8888;
  attachment.alpha_semantics = GWIPC_ALPHA_PREMULTIPLIED;
  const bool second = enqueue_contract(connection, GWIPC_MESSAGE_BUFFER_ATTACH,
      GWIPC_FLAG_SNAPSHOT_ITEM, attachment, gwipc_contract_encode_buffer_attach,
      &overlay_fd, 1);
  (void)::close(background_fd); (void)::close(overlay_fd);
  if (!first || !second) return false;

  std::vector<std::uint8_t> end;
  put_u64(end, 1); put_u64(end, 1); put_u32(end, 5); put_u32(end, 0);
  if (!enqueue(connection, GWIPC_MESSAGE_SNAPSHOT_END, 0, end.data(), end.size()))
    return false;
  gwipc_frame_commit commit{};
  commit.struct_size = sizeof(commit); commit.commit_id = 100;
  commit.output_id = 1; commit.producer_generation = 1;
  return enqueue_contract(connection, GWIPC_MESSAGE_FRAME_COMMIT,
                          GWIPC_FLAG_ACK_REQUIRED, commit,
                          gwipc_contract_encode_frame_commit);
}

bool await_ack(gwipc_connection* connection) {
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (!pump(connection, 50)) return false;
    gwipc_message* raw = nullptr;
    if (gwipc_connection_receive(connection, &raw) != GWIPC_STATUS_OK) continue;
    const Message message(raw);
    if (gwipc_message_type(message.get()) != GWIPC_MESSAGE_FRAME_ACKNOWLEDGED)
      continue;
    gwipc_decoded_contract* decoded = nullptr;
    if (gwipc_contract_decode_message(message.get(), &decoded) != GWIPC_STATUS_OK)
      return false;
    const auto* ack = gwipc_decoded_frame_acknowledged(decoded);
    const bool accepted = ack && ack->commit_id == 100 &&
                          ack->result == GWIPC_FRAME_ACCEPTED;
    gwipc_decoded_contract_destroy(decoded);
    return accepted;
  }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--help") { usage(stdout); return 0; }
  const char* socket = nullptr;
  std::string_view scenario;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if ((argument == "--socket" || argument == "--scenario") && index + 1 < argc) {
      if (argument == "--socket") socket = argv[++index];
      else scenario = argv[++index];
    } else { usage(stderr); return 2; }
  }
  if (!socket || *socket == '\0' || scenario.empty()) { usage(stderr); return 2; }
  bool known = false;
  for (const auto candidate : kScenarios) known |= candidate == scenario;
  if (!known) { std::fprintf(stderr, "gwcomp_m4_producer: unknown scenario: %.*s\n",
      static_cast<int>(scenario.size()), scenario.data()); return 2; }
  if (scenario != "basic") {
    std::fprintf(stderr, "gwcomp_m4_producer: scenario not implemented yet: %.*s\n",
                 static_cast<int>(scenario.size()), scenario.data());
    return 2;
  }

  gwipc_connection_options options{};
  options.struct_size = sizeof(options); options.path = socket;
  options.local_role = GWIPC_ROLE_TEST_PRODUCER;
  options.acceptable_server_roles = GWIPC_ROLE_BIT(GWIPC_ROLE_COMPOSITOR);
  options.offered_capabilities = kCapabilities;
  options.required_peer_capabilities = kCapabilities;
  options.instance_label = "gwcomp-m4-producer";
  gwipc_connection* raw = nullptr;
  auto status = gwipc_connection_connect(&options, &raw);
  if (status != GWIPC_STATUS_OK && status != GWIPC_STATUS_IN_PROGRESS) return 1;
  const Connection connection(raw);
  for (int attempt = 0; attempt < 200 &&
       gwipc_connection_get_state(connection.get()) != GWIPC_CONNECTION_ESTABLISHED;
       ++attempt) if (!pump(connection.get(), 50)) return 1;
  if (gwipc_connection_get_state(connection.get()) != GWIPC_CONNECTION_ESTABLISHED ||
      !send_basic(connection.get()) || !await_ack(connection.get())) return 1;
  return 0;
}
