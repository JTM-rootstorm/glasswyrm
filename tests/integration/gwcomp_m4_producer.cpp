#include <glasswyrm/ipc.h>

#include "gwcomp_m4_edge_scenarios.hpp"

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
struct ControlPayloadDeleter { void operator()(gwipc_control_payload* value) const { gwipc_control_payload_destroy(value); } };
using Connection = std::unique_ptr<gwipc_connection, ConnectionDeleter>;
using Message = std::unique_ptr<gwipc_message, MessageDeleter>;
using Payload = std::unique_ptr<gwipc_contract_payload, PayloadDeleter>;
using ControlPayload = std::unique_ptr<gwipc_control_payload, ControlPayloadDeleter>;

void usage(FILE* stream) {
  std::fprintf(stream,
               "Usage: gwcomp_m4_producer --socket PATH --scenario NAME\n"
               "Scenarios: basic, damage-update, stacking, visibility, "
               "clipping, opacity, buffer-replace, detach-remove, "
               "invalid-metadata, invalid-buffer, snapshot-reconnect, "
               "unknown-reference\n");
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

template <class Value, class Encoder>
bool enqueue_control(gwipc_connection* connection, std::uint16_t type,
                     const Value& value, Encoder encoder) {
  gwipc_control_payload* raw = nullptr;
  if (encoder(&value, &raw) != GWIPC_STATUS_OK) return false;
  const ControlPayload payload(raw);
  std::size_t size = 0;
  const auto* bytes = gwipc_control_payload_data(payload.get(), &size);
  return enqueue(connection, type, 0, bytes, size);
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

gwipc_surface_upsert surface(std::uint64_t id) {
  gwipc_surface_upsert value{};
  value.struct_size = sizeof(value); value.surface_id = id; value.output_id = 1;
  value.logical_width = 320; value.logical_height = 200; value.visible = 1;
  value.transform = GWIPC_TRANSFORM_NORMAL; value.opacity = GWIPC_OPACITY_ONE;
  value.scale_numerator = 1; value.scale_denominator = 1; value.color = srgb();
  return value;
}

bool send_commit(gwipc_connection* connection, std::uint64_t commit_id) {
  gwipc_frame_commit commit{};
  commit.struct_size = sizeof(commit); commit.commit_id = commit_id;
  commit.output_id = 1; commit.producer_generation = commit_id - 99;
  return enqueue_contract(connection, GWIPC_MESSAGE_FRAME_COMMIT,
                          GWIPC_FLAG_ACK_REQUIRED, commit,
                          gwipc_contract_encode_frame_commit);
}

bool send_basic(gwipc_connection* connection, int* writable_background_fd) {
  gwipc_snapshot_begin begin{sizeof(begin), 1, GWIPC_SNAPSHOT_COMPLETE_SESSION,
                             0, 1, 5, {}};
  if (!enqueue_control(connection, GWIPC_MESSAGE_SNAPSHOT_BEGIN, begin,
                       gwipc_control_encode_snapshot_begin))
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

  gwipc_surface_upsert background = surface(1);
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
  if (writable_background_fd) *writable_background_fd = background_fd;
  else (void)::close(background_fd);
  (void)::close(overlay_fd);
  if (!first || !second) return false;

  gwipc_snapshot_end end{sizeof(end), 1, 1, 5, {}};
  if (!enqueue_control(connection, GWIPC_MESSAGE_SNAPSHOT_END, end,
                       gwipc_control_encode_snapshot_end))
    return false;
  return send_commit(connection, 100);
}

bool await_ack(gwipc_connection* connection, std::uint64_t commit_id) {
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
    const bool accepted = ack && ack->commit_id == commit_id &&
                          ack->result == GWIPC_FRAME_ACCEPTED;
    gwipc_decoded_contract_destroy(decoded);
    return accepted;
  }
  return false;
}

bool update_surface(gwipc_connection* connection, const gwipc_surface_upsert& value,
                    std::uint64_t commit_id) {
  return enqueue_contract(connection, GWIPC_MESSAGE_SURFACE_UPSERT, 0, value,
                          gwipc_contract_encode_surface_upsert) &&
         send_commit(connection, commit_id) && await_ack(connection, commit_id);
}

bool run_damage_update(gwipc_connection* connection, int background_fd) {
  constexpr std::size_t size = 320U * 200U * 4U;
  void* mapping = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                         background_fd, 0);
  if (mapping == MAP_FAILED) return false;
  auto* pixels = static_cast<std::uint32_t*>(mapping);
  for (std::size_t y = 20; y < 28; ++y)
    for (std::size_t x = 24; x < 40; ++x) pixels[y * 320U + x] = UINT32_C(0xffe0c020);
  const bool synced = ::msync(mapping, size, MS_SYNC) == 0;
  (void)::munmap(mapping, size);
  if (!synced) return false;
  const gwipc_damage_rectangle rectangle{24, 20, 16, 8};
  gwipc_surface_damage damage{};
  damage.struct_size = sizeof(damage); damage.surface_id = 1;
  damage.rectangles = &rectangle; damage.rectangle_count = 1;
  return enqueue_contract(connection, GWIPC_MESSAGE_SURFACE_DAMAGE, 0, damage,
                          gwipc_contract_encode_surface_damage) &&
         send_commit(connection, 101) && await_ack(connection, 101);
}

bool run_scenario(gwipc_connection* connection, std::string_view scenario,
                  int background_fd) {
  if (scenario == "basic") return true;
  if (scenario == "damage-update") return run_damage_update(connection, background_fd);
  auto background = surface(1);
  auto overlay = surface(2);
  overlay.logical_x = 80; overlay.logical_y = 50;
  overlay.logical_width = 80; overlay.logical_height = 60; overlay.stacking = 1;
  if (scenario == "stacking") {
    background.stacking = 2;
    overlay.stacking = -1;
    return enqueue_contract(connection, GWIPC_MESSAGE_SURFACE_UPSERT, 0, background,
                            gwipc_contract_encode_surface_upsert) &&
           update_surface(connection, overlay, 101);
  }
  if (scenario == "visibility") {
    overlay.visible = 0;
    if (!update_surface(connection, overlay, 101)) return false;
    overlay.visible = 1;
    return update_surface(connection, overlay, 102);
  }
  if (scenario == "clipping") {
    overlay.logical_x = -20; overlay.logical_y = 175;
    overlay.clipping = 1; overlay.clip_x = 10; overlay.clip_y = 5;
    overlay.clip_width = 50; overlay.clip_height = 40;
    if (!update_surface(connection, overlay, 101)) return false;
    overlay.clip_x = 20; overlay.clip_y = 10;
    overlay.clip_width = 30; overlay.clip_height = 20;
    return update_surface(connection, overlay, 102);
  }
  if (scenario == "opacity") {
    overlay.opacity = 0;
    if (!update_surface(connection, overlay, 101)) return false;
    overlay.opacity = GWIPC_OPACITY_ONE / 2;
    if (!update_surface(connection, overlay, 102)) return false;
    overlay.opacity = GWIPC_OPACITY_ONE;
    if (!update_surface(connection, overlay, 103)) return false;
    overlay.opacity = GWIPC_OPACITY_ONE / 2;
    return update_surface(connection, overlay, 104);
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
  const int edge_result = run_gwcomp_m4_edge_scenario(socket, scenario);
  if (edge_result >= 0) return edge_result;
  if (scenario != "basic" && scenario != "damage-update" &&
      scenario != "stacking" && scenario != "visibility" &&
      scenario != "clipping" && scenario != "opacity") {
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
  int background_fd = -1;
  if (gwipc_connection_get_state(connection.get()) != GWIPC_CONNECTION_ESTABLISHED ||
      !send_basic(connection.get(), scenario == "damage-update" ? &background_fd : nullptr) ||
      !await_ack(connection.get(), 100) ||
      !run_scenario(connection.get(), scenario, background_fd)) {
    if (background_fd >= 0) (void)::close(background_fd);
    return 1;
  }
  if (background_fd >= 0) (void)::close(background_fd);
  return 0;
}
